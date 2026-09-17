#!/usr/bin/env python3
"""Small Modbus RTU slave for Linux pseudo-terminal integration tests.

It supports function 0x03 (read holding registers) and 0x06 (write one
holding register). It is deliberately dependency-free so it can run on a
development PC next to the gateway process.
"""

import os
import pty
import select
import signal
import sys
import tty


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def add_crc(data: bytes) -> bytes:
    value = crc16(data)
    return data + bytes((value & 0xFF, value >> 8))


def serve_request(frame: bytes, registers: dict[int, int]) -> bytes | None:
    if len(frame) != 8 or crc16(frame[:-2]) != int.from_bytes(frame[-2:], "little"):
        return None
    slave, function = frame[0], frame[1]
    address = int.from_bytes(frame[2:4], "big")
    argument = int.from_bytes(frame[4:6], "big")
    if function == 0x06:
        registers[address] = argument
        return frame
    if function == 0x03 and 1 <= argument <= 125:
        values = b"".join(registers.get(address + i, 0).to_bytes(2, "big")
                          for i in range(argument))
        return add_crc(bytes((slave, function, len(values))) + values)
    return add_crc(bytes((slave, function | 0x80, 0x01)))


def main() -> int:
    master, slave = pty.openpty()
    tty.setraw(master)
    tty.setraw(slave)
    slave_path = os.ttyname(slave)
    print(slave_path, flush=True)
    print("Use serial.device above and set serial.rs485=false", file=sys.stderr, flush=True)

    running = True

    def stop(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    buffer = bytearray()
    registers: dict[int, int] = {0x0010: 0}

    while running:
        readable, _, _ = select.select([master], [], [], 0.2)
        if not readable:
            continue
        chunk = os.read(master, 256)
        if not chunk:
            break
        buffer.extend(chunk)
        while len(buffer) >= 8:
            frame = bytes(buffer[:8])
            response = serve_request(frame, registers)
            if response is None:
                del buffer[0]
                continue
            del buffer[:8]
            print(f"RX {frame.hex(' ')} -> TX {response.hex(' ')}", file=sys.stderr)
            os.write(master, response)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
