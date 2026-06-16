#!/usr/bin/env python3
"""ws_capture.py — lossless recorder for the firmware's /canstream WebSocket.

Connects to ws://<ip>/canstream, records every line (@TX / @RX / leveled logs) to a
.canlog with a relative-ms timestamp, for `duration` seconds. The WS stream carries
@RX (the real radio's frames) which the USB serial does NOT — so this is the tool for
capturing inbound bus traffic (e.g. the 0x1F1 planet-image ISO-TP burst).

    python ws_capture.py <ip> <out.canlog> <seconds>
    python ws_capture.py 192.168.100.85 planet.canlog 30
"""
import sys, time, websocket

ip   = sys.argv[1] if len(sys.argv) > 1 else "192.168.100.85"
out  = sys.argv[2] if len(sys.argv) > 2 else "planet.canlog"
dur  = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0
url  = "ws://%s/canstream" % ip

ws = websocket.create_connection(url, timeout=6)
ws.settimeout(1.0)
t0 = time.time()
n = 0
with open(out, "w", encoding="utf-8") as f:
    f.write("# MeganeCAN ws capture %s\n" % url)
    f.flush()
    print("recording %s -> %s for %.0fs ... restart the radio NOW" % (url, out, dur), flush=True)
    while time.time() - t0 < dur:
        try:
            msg = ws.recv()
        except websocket.WebSocketTimeoutException:
            continue
        except Exception as e:
            print("ws error: %s" % e, flush=True)
            break
        ms = int((time.time() - t0) * 1000)
        for line in str(msg).split("\n"):     # WsWireLink batches lines per frame
            line = line.strip()
            if line:
                f.write("%d %s\n" % (ms, line))
                n += 1
        f.flush()
try:
    ws.close()
except Exception:
    pass
print("done: %d lines -> %s" % (n, out), flush=True)
