#include "TouchFrame.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    TPFrame input = {
        .sequence = 0x01020304,
        .capture_time_us = 0x0102030405060708ULL,
        .device_time_us = 125000001,
        .contact_count = 1,
        .contacts = {{
            .identifier = 42,
            .state = 4,
            .flags = TP_CONTACT_IN_RANGE | TP_CONTACT_TIP | TP_CONTACT_CONFIDENCE,
            .x = 0.25f,
            .y = 0.75f,
            .velocity_x = -1.5f,
            .velocity_y = 2.0f,
            .size = 0.5f,
            .angle = 1.570796f,
            .major_axis = 8.0f,
            .minor_axis = 7.0f,
            .density = 1.25f,
        }},
    };
    uint8_t wire[TP_MAX_MESSAGE_SIZE];
    const size_t size = tp_encode_message(TP_MESSAGE_FRAME, &input, wire, sizeof(wire));
    assert(size == TP_HEADER_SIZE + TP_CONTACT_SIZE);
    assert(memcmp(wire, "MTP1", 4) == 0);
    assert(wire[12] == 1 && wire[13] == 2 && wire[14] == 3 && wire[15] == 4);

    TPFrame output;
    enum TPMessageType type;
    assert(tp_decode_message(wire, size, &type, &output));
    assert(type == TP_MESSAGE_FRAME);
    assert(output.sequence == input.sequence);
    assert(output.capture_time_us == input.capture_time_us);
    assert(output.contact_count == 1);
    assert(output.contacts[0].identifier == 42);
    assert(fabsf(output.contacts[0].x - 0.25f) < 0.00001f);
    assert(fabsf(output.contacts[0].velocity_x + 1.5f) < 0.00001f);

    wire[0] = 0;
    assert(!tp_decode_message(wire, size, &type, &output));
    puts("protocol_test: ok");
    return 0;
}
