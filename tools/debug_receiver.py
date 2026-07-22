#!/usr/bin/env python3
import argparse
import socket
import struct

MAGIC = b"MTP1"
HEADER = struct.Struct(">4sHHIIHHQQ")
CONTACT = struct.Struct(">IBBHfffffffff")
TYPE_NAMES = {1: "HELLO", 2: "FRAME", 3: "RESET"}


def receive_exact(connection, length):
    chunks = []
    remaining = length
    while remaining:
        chunk = connection.recv(remaining)
        if not chunk:
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def main():
    parser = argparse.ArgumentParser(description="Reference MTP1 TCP receiver")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=39871)
    parser.add_argument("--once", action="store_true", help="exit when the first client disconnects")
    args = parser.parse_args()

    with socket.create_server((args.host, args.port), reuse_port=False) as server:
        print(f"listening on {args.host}:{args.port}", flush=True)
        while True:
            connection, address = server.accept()
            print(f"client {address[0]}:{address[1]} connected", flush=True)
            with connection:
                while True:
                    header_bytes = receive_exact(connection, HEADER.size)
                    if header_bytes is None:
                        break
                    magic, version, message_type, payload_length, sequence, count, flags, capture_us, device_us = HEADER.unpack(header_bytes)
                    if magic != MAGIC or version != 1 or payload_length != count * CONTACT.size:
                        raise ValueError("invalid MTP1 header")
                    payload = receive_exact(connection, payload_length)
                    if payload is None:
                        raise EOFError("connection ended inside payload")
                    contacts = []
                    for offset in range(0, payload_length, CONTACT.size):
                        values = CONTACT.unpack_from(payload, offset)
                        contacts.append({
                            "id": values[0],
                            "state": values[1],
                            "flags": values[2],
                            "x": round(values[4], 5),
                            "y": round(values[5], 5),
                        })
                    print({
                        "type": TYPE_NAMES.get(message_type, message_type),
                        "sequence": sequence,
                        "flags": flags,
                        "capture_us": capture_us,
                        "device_us": device_us,
                        "contacts": contacts,
                    }, flush=True)
            print("client disconnected", flush=True)
            if args.once:
                return


if __name__ == "__main__":
    main()
