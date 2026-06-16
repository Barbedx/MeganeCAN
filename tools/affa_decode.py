#!/usr/bin/env python3
"""affa_decode.py — reassemble AFFA3 ISO-TP messages from a serial log and decode them.

Parses every CAN frame line in a log (both the firmware's "[TX]/[RX] ID: 0x.. Data: { .. }"
dumps and the "@TX <id> <bytes>" / "@RX <id> <bytes>" wire lines), reassembles the
ISO-TP stream per CAN id+direction, and prints one decoded line per complete message:

    0x151 RX  len=96  cmd=21 mode=05  FULLSCREEN   text='Please insert\r navigation CD\r'

ISO-TP (11-bit) framing as used by AFFA3 on 0x151:
  single  : 0L           + L data bytes
  first   : 1L LL        + 6 data bytes   (len = 0xLLL, 12-bit)
  consec  : 2N           + 7 data bytes   (N = rolling sequence)
  flow    : 3x           (control, ignored for content)

Usage:  python affa_decode.py LOGFILE [--id 151] [--raw] [--all-ids]
"""
import re
import sys

# "[RX] ID: 0x151 Len: 8 Data: { 10 60 21 05 FF 00 00 40 }"
DUMP_RE = re.compile(
    r"\[(TX|RX)\]\s*ID:\s*0x([0-9A-Fa-f]+).*?Data:\s*\{\s*([0-9A-Fa-f ]+?)\s*\}")
# "@TX 151 10 60 21 05 FF 00 00 40"
WIRE_RE = re.compile(r"@(TX|RX)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f ]+)")


def cmd_name(content):
    """Classify a reassembled AFFA3 0x151 payload by its command/mode bytes."""
    if not content:
        return "(empty)"
    c0 = content[0]
    c1 = content[1] if len(content) > 1 else 0
    if c0 == 0x21:
        mode = {0x01: "MENU/window", 0x05: "FULLSCREEN"}.get(c1, "mode=0x%02X" % c1)
        return "screen 0x21 [%s]" % mode
    if c0 == 0x76:
        return "info-menu 0x76 sub=0x%02X" % c1
    if c0 == 0x77:
        return "popup 0x77 sub=0x%02X" % c1
    return "cmd=0x%02X sub=0x%02X" % (c0, c1)


def to_text(content, start=0):
    out = []
    for b in content[start:]:
        if b == 0x0D:
            out.append("\\r")
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        elif b == 0x00:
            out.append("·")
        else:
            out.append("\\x%02X" % b)
    return "".join(out)


class Reasm:
    """Per (id,dir) ISO-TP reassembler. feed() returns a finished message or None."""
    def __init__(self):
        self.buf = []
        self.need = 0
        self.active = False

    def feed(self, data):
        if not data:
            return None
        pci = data[0] & 0xF0
        if pci == 0x00:                       # single frame
            n = data[0] & 0x0F
            return list(data[1:1 + n])
        if pci == 0x10:                       # first frame
            self.need = ((data[0] & 0x0F) << 8) | data[1]
            self.buf = list(data[2:8])
            self.active = True
            return self._maybe_done()
        if pci == 0x20:                       # consecutive frame
            if not self.active:
                return None
            self.buf.extend(data[1:8])
            return self._maybe_done()
        return None                            # flow control / unknown

    def _maybe_done(self):
        if self.active and len(self.buf) >= self.need:
            msg = self.buf[:self.need]
            self.buf, self.active = [], False
            return msg
        return None


def merge(path, want_id="151", direction="RX"):
    """Overlay ISO-TP fragments of a STATIC screen by sequence index, reconstructing the
    full payload even when the log is lossy (dropped CFs from serial saturation). Last
    non-zero writer wins per offset. Use for a capture that shows one steady screen."""
    want = want_id.upper().lstrip("0") or "0"
    slots = {}            # offset -> byte
    declared = 0
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = DUMP_RE.search(line) or WIRE_RE.search(line)
            if not m:
                continue
            d, idhex, payload = m.group(1), m.group(2).upper().lstrip("0") or "0", m.group(3)
            if idhex != want or d != direction:
                continue
            try:
                data = [int(x, 16) for x in payload.split()]
            except ValueError:
                continue
            pci = data[0] & 0xF0
            if pci == 0x10:
                declared = max(declared, ((data[0] & 0x0F) << 8) | data[1])
                for i, b in enumerate(data[2:8]):
                    if b or slots.get(i, 0) == 0:
                        slots[i] = b
            elif pci == 0x20:
                n = data[0] & 0x0F
                if n == 0:
                    n = 16
                base = 6 + (n - 1) * 7
                for i, b in enumerate(data[1:8]):
                    off = base + i
                    if b or slots.get(off, 0) == 0:
                        slots[off] = b
    if not slots:
        print("  (no %s frames on 0x%s)" % (direction, want))
        return
    n = declared or (max(slots) + 1)
    buf = [slots.get(i, 0) for i in range(n)]
    print("MERGED 0x%s %s  declared_len=%d" % (want, direction, declared))
    print("  %s" % cmd_name(buf))
    print("  hex : %s" % " ".join("%02X" % b for b in buf))
    print("  text: '%s'" % to_text(buf))


def parse(path, want_id=None, all_ids=False, raw=False):
    frames = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = DUMP_RE.search(line)
            if m:
                d, idhex, payload = m.group(1), m.group(2), m.group(3)
            else:
                m = WIRE_RE.search(line)
                if not m:
                    continue
                d, idhex, payload = m.group(1), m.group(2), m.group(3)
            try:
                data = [int(x, 16) for x in payload.split()]
            except ValueError:
                continue
            frames.append((d, idhex.upper().lstrip("0") or "0", data))

    reasm = {}
    last = {}
    for d, idhex, data in frames:
        if not all_ids and want_id and idhex != want_id.upper().lstrip("0"):
            continue
        if not all_ids and not want_id and idhex != "151":
            continue
        key = (idhex, d)
        r = reasm.setdefault(key, Reasm())
        if raw:
            print("  %s 0x%s  %s" % (d, idhex, " ".join("%02X" % b for b in data)))
        msg = r.feed(data)
        if msg is not None:
            sig = (idhex, d, tuple(msg))
            if sig == last.get(key):
                continue                       # collapse identical repeats
            last[key] = sig
            hexs = " ".join("%02X" % b for b in msg)
            head = "0x%s %s len=%-3d %s" % (idhex, d, len(msg), cmd_name(msg))
            print(head)
            print("        hex : %s" % hexs)
            print("        text: '%s'" % to_text(msg))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    path = sys.argv[1]
    want_id = None
    all_ids = "--all-ids" in sys.argv
    raw = "--raw" in sys.argv
    if "--id" in sys.argv:
        want_id = sys.argv[sys.argv.index("--id") + 1]
    if "--merge" in sys.argv:
        merge(path, want_id or "151")
        return
    parse(path, want_id, all_ids, raw)


if __name__ == "__main__":
    main()
