#!/usr/bin/env python3
"""Minimal USB transfer smoke test for the ESP32 brosche.

Usage:
  python3 tools/usb_transfer_test.py [--port /dev/ttyACM0] [--size 4096] [--name test.bin]

If --file PATH is provided, the bytes from PATH are sent instead of random data.
"""
from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path
from typing import Iterable

import serial  # type: ignore

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200
DEFAULT_SIZE = 4096
DEFAULT_NAME = "usb_test.bin"


def iter_lines(ser: serial.Serial, timeout: float = 5.0) -> Iterable[str]:
    end = time.time() + timeout
    while time.time() < end:
        raw = ser.readline()
        if not raw:
            continue
        yield raw.decode("utf-8", "replace").strip()


def expect_ok(prefix: str, lines: Iterable[str]) -> None:
    for line in lines:
        print(line)
        if line.startswith("USB OK") or line.startswith("USB ERR"):
            if line.startswith("USB OK" + prefix):
                return
            if line.startswith("USB ERR"):
                raise RuntimeError(f"Device returned error: {line}")
    raise RuntimeError(f"Did not observe expected response prefix: {prefix}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Send a test payload over the USB transfer protocol.")
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE)
    parser.add_argument("--name", default=DEFAULT_NAME)
    parser.add_argument("--file", type=Path)
    parser.add_argument("--chunk", type=int, default=None, help="chunk size for paced writes (default: single write)")
    parser.add_argument("--delay", type=float, default=0.0, help="delay in seconds between chunk writes")
    args = parser.parse_args()

    if args.file:
        data = args.file.read_bytes()
        filename = args.file.name
    else:
        data = bytes((i % 256 for i in range(args.size)))
        filename = args.name

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    try:
        ser.reset_input_buffer()
        ser.write(b"PING\n")
        ser.flush()
        for line in iter_lines(ser, timeout=2):
            print(line)
            if line.startswith("USB OK PONG"):
                break
        else:
            raise RuntimeError("No PONG response; ensure USB transfer mode is active")

        ser.write(f"START {len(data)} {filename}\n".encode("ascii"))
        ser.flush()
        expect_ok(" START", iter_lines(ser, timeout=4))

        if args.chunk and args.chunk > 0:
            offset = 0
            while offset < len(data):
                end = min(offset + args.chunk, len(data))
                written = ser.write(data[offset:end])
                ser.flush()
                await_empty_start = time.time()
                while ser.out_waiting:
                    if time.time() - await_empty_start > 2:
                        raise RuntimeError("Serial out_waiting did not drain")
                    time.sleep(0.01)
                offset += written
                if args.delay:
                    time.sleep(args.delay)
        else:
            ser.write(data)
            ser.flush()

        # Wait for ESP32 to finish receiving (no PROG messages during transfer anymore)
        # Calculate expected transfer time and add margin
        expected_time = (len(data) * 10) / args.baud  # 10 bits per byte
        time.sleep(expected_time + 1.0)  # Add 1 second margin

        ser.write(b"END\n")
        ser.flush()
        for line in iter_lines(ser, timeout=10):
            print(line)
            if line.startswith("USB OK PROG"):  # May receive final PROG before END
                continue
            if line.startswith("USB OK END"):
                break
        else:
            raise RuntimeError("No END confirmation received")

        print("Transfer completed successfully")
    finally:
        ser.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(1)
