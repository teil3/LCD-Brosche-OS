#!/usr/bin/env python3
"""
System Image Upload Tool
Uploads images to ESP32 LittleFS /system/ directory via USB Serial

Usage:
    python3 upload_system_image.py <port> <image_file>

Example:
    python3 upload_system_image.py /dev/ttyACM0 ../assets/boot_logo_200.jpg
"""

import sys
import time
import serial
import os
from pathlib import Path


BAUD_RATE = 115200
CHUNK_SIZE = 1024
TIMEOUT = 2.0
TARGET_DIR = "/system"


def wait_for_response(ser, timeout=2.0):
    """Wait for and return a response line from the ESP32."""
    start = time.time()
    line = ""
    while (time.time() - start) < timeout:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            line += data
            if '\n' in line:
                lines = line.split('\n')
                for l in lines[:-1]:
                    l = l.strip()
                    if l:
                        return l
                line = lines[-1]
        time.sleep(0.01)
    return None


def send_ping(ser):
    """Send PING command and wait for PONG response."""
    ser.write(b"PING\n")
    response = wait_for_response(ser, timeout=1.0)
    return response and "PONG" in response


def upload_image(port, image_path, target_dir=TARGET_DIR):
    """Upload an image file to ESP32 LittleFS."""

    if not os.path.exists(image_path):
        print(f"❌ Error: File not found: {image_path}")
        return False

    file_size = os.path.getsize(image_path)
    filename = Path(image_path).name

    print(f"📁 File: {filename}")
    print(f"📊 Size: {file_size} bytes ({file_size/1024:.1f} KB)")
    print(f"📂 Target: {target_dir}/{filename}")
    print(f"🔌 Port: {port}")
    print()

    try:
        # Open serial connection
        print("🔗 Opening serial connection...")
        ser = serial.Serial(port, BAUD_RATE, timeout=TIMEOUT)
        time.sleep(0.5)  # Wait for ESP32 to stabilize

        # Clear any pending data
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # Send PING to verify connection
        print("🏓 Sending PING...")
        if not send_ping(ser):
            print("⚠️  Warning: No PONG response (continuing anyway)")
        else:
            print("✅ PONG received")

        # Send START command with target directory
        start_cmd = f"START {file_size} {filename} {target_dir}\n"
        print(f"📤 Sending: {start_cmd.strip()}")
        ser.write(start_cmd.encode('utf-8'))

        # Wait for START response
        response = wait_for_response(ser, timeout=3.0)
        if response:
            print(f"📥 Response: {response}")
            if "ERR" in response:
                print(f"❌ Error from ESP32: {response}")
                ser.close()
                return False

        # Send file data in chunks
        print(f"📤 Uploading data...")
        with open(image_path, 'rb') as f:
            sent = 0
            last_percent = -1

            while sent < file_size:
                chunk = f.read(CHUNK_SIZE)
                if not chunk:
                    break

                ser.write(chunk)
                sent += len(chunk)

                # Progress indicator
                percent = int((sent / file_size) * 100)
                if percent != last_percent and percent % 10 == 0:
                    print(f"   {percent}% ({sent}/{file_size} bytes)")
                    last_percent = percent

        print(f"✅ Upload complete: {sent} bytes sent")

        # Send END command
        print("📤 Sending END command...")
        ser.write(b"END\n")

        # Wait for completion response (may receive PROG then END)
        # The ESP32 sends PROG immediately, then END after writing to flash
        time.sleep(0.5)  # Give ESP32 time to write to flash

        result = False
        end_received = False
        prog_received = False

        # Read all responses until we get END or timeout
        print("📥 Waiting for responses...")
        for attempt in range(15):  # Try up to 15 times (30 seconds total)
            response = wait_for_response(ser, timeout=2.0)
            if response:
                print(f"📥 Response: {response}")

                # Track if we got PROG
                if "USB OK PROG" in response:
                    prog_received = True

                # Skip slideshow messages
                if "[Slideshow]" in response:
                    continue

                # Check for END confirmation
                if "USB OK END" in response:
                    end_received = True
                    break

            # If no more responses and we got PROG, that's good enough
            if not response and prog_received:
                print("📥 No more responses, but PROG was received")
                break

        if end_received:
            print(f"✅ SUCCESS: File uploaded to {target_dir}/{filename}")
            result = True
        elif prog_received:
            print(f"✅ SUCCESS: File uploaded (PROG confirmed, END may have been missed)")
            result = True
        else:
            print(f"⚠️  Warning: Upload status unclear")
            result = False

        # Read any additional responses
        time.sleep(0.2)
        while ser.in_waiting > 0:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            if data.strip():
                print(f"📥 {data.strip()}")

        ser.close()
        return result

    except serial.SerialException as e:
        print(f"❌ Serial error: {e}")
        return False
    except KeyboardInterrupt:
        print("\n⚠️  Upload cancelled by user")
        try:
            ser.write(b"ABORT\n")
            ser.close()
        except:
            pass
        return False
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        return False


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        print("Available serial ports:")
        try:
            from serial.tools import list_ports
            for port in list_ports.comports():
                print(f"  - {port.device}: {port.description}")
        except:
            print("  (install pyserial to list ports: pip install pyserial)")
        sys.exit(1)

    port = sys.argv[1]
    image_path = sys.argv[2]

    # Optional: custom target directory
    target_dir = sys.argv[3] if len(sys.argv) > 3 else TARGET_DIR

    print("=" * 60)
    print("ESP32 System Image Upload Tool")
    print("=" * 60)
    print()

    success = upload_image(port, image_path, target_dir)

    print()
    print("=" * 60)
    if success:
        print("✅ Upload successful!")
    else:
        print("❌ Upload failed!")
    print("=" * 60)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
