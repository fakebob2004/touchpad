#!/usr/bin/env python3
"""Send a short synthetic one-finger MTP1 gesture to the Windows receiver."""

import argparse
import math
import socket
import struct
import time


HEADER = struct.Struct(">4sHHIIHHQQ")
CONTACT = struct.Struct(">IBBH9f")


def message(message_type: int, sequence: int, contacts=()) -> bytes:
    payload = b"".join(
        CONTACT.pack(
            identifier,
            4,          # Touching state used by the macOS producer.
            0x07,       # In Range + Tip Switch + Confidence.
            0,
            x,
            y,
            velocity_x,
            velocity_y,
            0.08,       # Size.
            0.0,        # Angle.
            0.08,       # Major axis.
            0.06,       # Minor axis.
            1.0,        # Density.
        )
        for identifier, x, y, velocity_x, velocity_y in contacts
    )
    now_us = time.monotonic_ns() // 1_000
    return HEADER.pack(
        b"MTP1", 1, message_type, len(payload), sequence,
        len(contacts), 0, now_us, now_us
    ) + payload


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=39871)
    parser.add_argument("--duration", type=float, default=3.0)
    parser.add_argument("--rate", type=float, default=125.0)
    args = parser.parse_args()

    sequence = 1
    with socket.create_connection((args.host, args.port), timeout=3.0) as client:
        client.sendall(message(1, sequence))  # HELLO
        sequence += 1
        client.sendall(message(3, sequence))  # RESET
        sequence += 1

        start = time.monotonic()
        previous_x = 0.5
        period = 1.0 / args.rate
        while (elapsed := time.monotonic() - start) < args.duration:
            x = 0.5 + 0.2 * math.sin(elapsed * math.tau / 1.5)
            velocity_x = (x - previous_x) * args.rate
            client.sendall(message(2, sequence, ((1, x, 0.5, velocity_x, 0.0),)))
            sequence += 1
            previous_x = x
            time.sleep(period)

        client.sendall(message(2, sequence))  # Explicit all-contacts-up frame.

    print("Synthetic gesture sent successfully.")


if __name__ == "__main__":
    main()
