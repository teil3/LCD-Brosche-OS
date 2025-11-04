#!/usr/bin/env python3
"""Listen LittleFS contents over the SerialImageTransfer protocol.

Requires the ESP32 to be im USB-Transfer-Modus. Für jedes Verzeichnis wird der
`LIST`-Befehl gesendet und die `USB OK LIST`/`LISTDONE`-Antworten ausgewertet.

Beispiel:

    python3 tools/list_littlefs.py --port /dev/ttyACM0 --root /system

"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import List, Tuple

import serial  # type: ignore


DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200


@dataclass
class Entry:
    path: PurePosixPath
    is_dir: bool
    size: int


def ping(ser: serial.Serial, timeout: float = 2.0) -> None:
    """Send PING and wait for a USB OK PONG."""

    ser.reset_input_buffer()
    ser.write(b"PING\n")
    ser.flush()
    end = time.time() + timeout
    while time.time() < end:
        line = ser.readline()
        if not line:
            continue
        text = line.decode("utf-8", "replace").strip()
        if text.startswith("USB OK PONG"):
            return
        if text.startswith("USB ERR"):
            raise RuntimeError(f"Device reported error: {text}")
    raise RuntimeError("No PONG response; ensure USB transfer mode is active")


def read_fsinfo(ser: serial.Serial, timeout: float = 2.0) -> Tuple[int, int, int]:
    ser.write(b"FSINFO\n")
    ser.flush()
    end = time.time() + timeout
    while time.time() < end:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", "replace").strip()
        if not text:
            continue
        if text.startswith("USB ERR"):
            raise RuntimeError(f"Device reported error during FSINFO: {text}")
        if text.startswith("USB OK FSINFO"):
            parts = text.split()
            if len(parts) >= 6:
                try:
                    total = int(parts[3])
                    used = int(parts[4])
                    free = int(parts[5])
                    return total, used, free
                except ValueError as exc:
                    raise RuntimeError(f"Invalid FSINFO payload: {text}") from exc
            raise RuntimeError(f"Unexpected FSINFO format: {text}")
    raise RuntimeError("Timeout while waiting for FSINFO response")


def send_list(ser: serial.Serial, directory: PurePosixPath, timeout: float = 5.0) -> Tuple[List[Entry], bool]:
    """Send LIST command for a directory and return entries.

    Returns a tuple (entries, success)."""

    cmd = "LIST" if directory == PurePosixPath("/") else f"LIST {directory.as_posix()}"
    ser.write(cmd.encode("ascii") + b"\n")
    ser.flush()

    entries: List[Entry] = []
    end = time.time() + timeout
    while time.time() < end:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", "replace").strip()
        if not text:
            continue

        if text.startswith("USB ERR"):
            return entries, False

        if text.startswith("USB OK LISTDONE"):
            return entries, True

        if text.startswith("USB OK LIST "):
            parts = text.split(" ", 6)
            if len(parts) < 6:
                continue
            entry_type = parts[3]
            name = parts[4]
            try:
                size = int(parts[5])
            except ValueError:
                size = 0

            if name in (".", ".."):
                continue

            if name.startswith("/"):
                child = PurePosixPath(name)
            else:
                child = PurePosixPath((directory / name).as_posix())

            entries.append(Entry(path=child, is_dir=(entry_type == "D"), size=size))
            continue

        # Ignore any other lines (debug output etc.)

    raise RuntimeError(f"Timeout while waiting for LIST response for {directory}")


def list_recursive(ser: serial.Serial, root: PurePosixPath) -> List[Entry]:
    stack = [root]
    discovered: List[Entry] = []
    visited = set()

    while stack:
        current = stack.pop()
        if current in visited:
            continue
        visited.add(current)

        entries, ok = send_list(ser, current)
        if not ok:
            raise RuntimeError(f"LIST failed for {current.as_posix()}")

        for entry in entries:
            discovered.append(entry)
            if entry.is_dir:
                stack.append(entry.path)

    discovered.sort(key=lambda e: e.path.as_posix())
    return discovered


def main() -> None:
    parser = argparse.ArgumentParser(description="List LittleFS contents via SerialImageTransfer LIST command")
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial port (default: %(default)s)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baud rate (default: %(default)d)")
    parser.add_argument("--root", default="/", help="Start directory (default: %(default)s)")
    parser.add_argument("--no-ping", action="store_true", help="Skip initial PING/PONG handshake")
    args = parser.parse_args()

    root = PurePosixPath(args.root)
    if not root.is_absolute():
        root = PurePosixPath("/") / root

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.5)
    except serial.SerialException as exc:  # type: ignore[attr-defined]
        raise SystemExit(f"Failed to open serial port {args.port}: {exc}")

    with ser:
        if not args.no_ping:
            ping(ser)
        fs_total, fs_used, fs_free = read_fsinfo(ser)
        entries = list_recursive(ser, root)

    if not entries:
        print("(empty)")
    else:
        for entry in entries:
            kind = "<DIR>" if entry.is_dir else "     "
            size = "-" if entry.is_dir else str(entry.size)
            print(f"{kind}  {size:>10}  {entry.path.as_posix()}")

    kb = 1024
    print(
        "\nLittleFS: total={:.1f} KB  used={:.1f} KB  free={:.1f} KB".format(
            fs_total / kb,
            fs_used / kb,
            fs_free / kb,
        )
    )


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(1)
