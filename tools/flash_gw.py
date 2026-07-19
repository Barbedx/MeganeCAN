#!/usr/bin/env python3
"""Flash the GW (WROVER) board THROUGH the DISP board's USB port (§3.4).

The DISP firmware bridges "@PROG <hex>" console lines onto the inter-board
link as raw ProgProto frames; GW's LinkTunnel writes its OTA partition and
reboots. Stop-and-wait per chunk, resumable on offset-mismatch replies.

    python tools/flash_gw.py COM5 .pio/build/gw-wrover/firmware.bin
"""
import sys
import time
import argparse

try:
    import serial
except ImportError:
    sys.exit("pyserial required: pip install pyserial")

OTA_BEGIN, OTA_DATA, OTA_END, OTA_STAT = 0x70, 0x71, 0x72, 0x73
CHUNK = 112          # 4B offset + data <= link payload cap


def send_frame(ser, ftype, payload=b""):
    ser.write(b"@PROG " + bytes([ftype]).hex().encode()
              + payload.hex().encode() + b"\n")


def wait_stat(ser, timeout):
    """Wait for an OTA_STAT echo: returns (code, detail) or None."""
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        line = ser.readline()
        if not line:
            continue
        buf = line.strip()
        if not buf.startswith(b"@PROG "):
            continue
        try:
            raw = bytes.fromhex(buf[6:].decode())
        except ValueError:
            continue
        if len(raw) >= 6 and raw[0] == OTA_STAT:
            code = raw[1]
            detail = int.from_bytes(raw[2:6], "little")
            return code, detail
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port", help="DISP board serial port (COMx / /dev/ttyACMx)")
    ap.add_argument("image", help="gw-wrover firmware.bin")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    data = open(args.image, "rb").read()
    size = len(data)
    print(f"image: {size} bytes")

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    time.sleep(0.5)
    ser.reset_input_buffer()

    print("OTA begin (GW erases its slot)...")
    send_frame(ser, OTA_BEGIN, size.to_bytes(4, "little"))
    st = wait_stat(ser, 15)
    if not st or st[0] != 0:
        sys.exit(f"begin failed: {st}")

    t0 = time.time()
    off = 0
    while off < size:
        chunk = data[off:off + CHUNK]
        send_frame(ser, OTA_DATA, off.to_bytes(4, "little") + chunk)
        st = wait_stat(ser, 5)
        if st is None:
            sys.exit(f"ack timeout at {off}")
        code, detail = st
        if code == 0:
            off = detail                      # next expected offset
        elif code == 2:
            print(f"\nresync: GW expects offset {detail}")
            off = detail                      # resend from where GW stands
        else:
            sys.exit(f"\nGW error {code} at {detail}")
        if off // CHUNK % 64 == 0:
            pct = 100.0 * off / size
            rate = off / max(time.time() - t0, 0.001) / 1024
            print(f"\r{pct:5.1f}%  {rate:6.1f} KB/s", end="", flush=True)

    print("\nOTA end (GW verifies + switches boot slot)...")
    send_frame(ser, OTA_END)
    st = wait_stat(ser, 25)
    if not st or st[0] != 0:
        sys.exit(f"end failed: {st}")
    print(f"done in {time.time() - t0:.1f}s — GW is rebooting into the new image")


if __name__ == "__main__":
    main()
