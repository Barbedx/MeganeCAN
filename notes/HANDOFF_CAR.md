# Handoff — on-car live CAN session (verify info/confirm, capture truth, drive panel)

Repo `c:\Users\Andru\source\repos\MeganeCAN`, branch `feature/icanbus-seams`, HEAD `ff28987`.
Read first: `notes/TRANSPORT.md`, `notes/MEMORY.md`, `notes/ARCHITECTURE.md`,
`notes/CARMINAT_DECOMPOSITION.md`. This file is the car-session continuation.

## Goal in the car
Live-listen the real CAN bus, and verify the AFFA3 screen commands against the REAL
Carminat panel — primarily **info-popup (`showInfoMenu`)** and **confirm-box
(`showConfirmBoxWithOffsets`)**. If our bytes are wrong: capture the genuine frames
from the OEM radio, fix the builders, and (only then) teach the decoder to read them.
Possibly also send our own frames to confirm the panel accepts them.

## State (what's done / flashed / pushed)
- **5 unpushed commits** on top of the autonomous push (`313a832`). User pushes:
  `git push -u origin feature/icanbus-seams`. They are:
  `c334df1` mem floor (+6KB, no leak — /wire WS was the cost), `21a40d4` notes/MEMORY.md,
  `23cf997` DisplayTransport (one route knob), `413c762` one screen decoder + /preview,
  `ff28987` /preview console + `/api/confirm`.
- **Board = `esp32dev-mini`** (ESP32-C3 SuperMini, native USB), enumerates as
  **COM6** ("USB Serial Device"). Display type NVS = **carminat**, BT bonded to the
  iPhone, ELM off, **`skip_funcreg = TRUE` (passive)**.
- ⚠️ **The mini was last flashed at the `413c762` era — `ff28987` (the /preview console
  + `/api/confirm`) is NOT on it yet. RE-FLASH before relying on those:**
  `pio run -e esp32dev-mini -t upload --upload-port COM6`
- Both ESP envs build; **native 26/26** (`pio test -e native`, needs
  `PATH=/c/Users/Andru/.platformio/penv/Scripts` and w64devkit for g++).

## Connect in the car (no home WiFi → AP)
AP **SSID `ESP32_MeganeCan_AP` / pass `Megane2004`** → phone joins → open
**`http://192.168.4.1`** (mDNS `meganecan.local` works only on home WiFi).
- **`/preview`** — the console: fire menu (+scroll arrows), info popup (3×8)+close,
  confirm box, display ON/OFF, switch route. Decoded box mirrors `/api/screen` for
  menu+highlight; **info/confirm must be judged by eye on the real panel** (the twin
  decoder does NOT decode them yet).
- **`/wire`** — live frame log + **Record → Save .canlog** (this is how you capture
  the truth). One decoded screen (from `/api/screen`, the single firmware decoder).
- Serial proxy (`tools/serial_proxy.py COM5/COMx 115200 8080`, `http://127.0.0.1:8080`)
  = raw serial log only now (no decoder). Holds the COM port exclusively.

## The skip_funcreg fork (decides which test you can do)
- **CAPTURE the OEM truth** → keep `skip_funcreg = TRUE` (passive): ESP only listens;
  every bus frame (incl. the radio's 0x151 info/confirm) is mirrored as `@RX` →
  `/wire` Record → `.canlog`. The real radio must actually produce the screen.
- **DRIVE the panel with OUR frames** → set `skip_funcreg = FALSE` (ESP becomes the
  radio, runs registration) via `POST /setskipfuncreg` (value `false`) → it restarts.
  In passive mode the OEM radio owns the panel and ignores our frames.

## Transport routes (one knob — `/api/route?mode=…`, see notes/TRANSPORT.md)
- `both` (default): real panel drives + ACKs, twin mirrors passively. **Car default.**
- `virtual`: no CAN; twin ACKs (desk/no-panel rendering).
- `can`: CAN only, twin idle.

## Verify workflow
1. (passive) Get the OEM radio to show info/confirm → `/wire` Record → Save `.canlog`.
2. Compare captured `@RX 151 …` bytes to our builders:
   - `showInfoMenu` — `CarminatDisplay.cpp` ~L805 + the free `::showInfoMenu` ~L? (sends
     `10 0B 76 <prefix> <offset> + 8 chars`, paired `0x21` continuation).
   - `showConfirmBoxWithOffsets` — `CarminatDisplay.cpp` ~L762 (ISO-TP `10 6F` + fixed
     6-byte header `21 05 00 00 01 49` + 105-byte content; caption@0x1A, rows@0x20).
3. Fix the builder bytes to match the capture (PROTOCOL-ADJACENT — see guardrails).
4. (skip_funcreg=FALSE) fire `/api/info` + `/api/confirm` from `/preview`, confirm the
   panel renders identically.
5. THEN teach the ONE decoder to read them so `/api/screen` + `/preview` + `/wire`
   mirror it: extend `src/affa/ScreenDecode.{h,cpp}` (add an info/confirm path) +
   `ScreenModel` (it already has `info[3][9]`/`infoCount`; confirm needs fields) +
   `CarminatVirtualDisplay::decode` (currently ignores `10 0B 76 …` — see the comment).

## Triggers / endpoints (all on the ESP, no PC tools)
- `GET /affa3/setMenu?caption=&name1=&name2=&scrollLock=0B` — menu (scrollLock hex:
  00 none / 0B ↓ / 07 ↑ / 0C ↕).
- `GET /api/info?l1=&l2=&l3=`  /  `GET /api/info/close`  — info popup (8 chars/slot).
- `GET /api/confirm?caption=&row1=&row2=` — confirm box. **NEW in ff28987.**
- `POST /display/state?enable=1|0` — panel ON / OFF (OFF = panel's own clock + outside
  temp from its built-in sensor).
- `POST /emulate/key key=<code> hold=0|1` — keys: Load=0 (hold=toggle menu),
  RollUp=257, RollDown=321, Vol+=3, Vol-=4, Src▶=1, ◀Src=2.
- `GET /api/screen` — the firmware-decoded ScreenModel (the ONE decoder) + `route` +
  `screenAge_ms`. `GET /api/health` — heap/uptime/ws/cfg.

## Display capability map (the 7 driver ops; what the twin decodes)
| op | what | decoded today? |
|---|---|---|
| `showMenu(hdr,r1,r2,scroll)` | menu / now-playing / notification (all showMenu) | ✅ |
| `highlightItem(0/1)` | selected row | ✅ (`sel`) |
| `showInfoMenu` | 3×8 info popup | ❌ decoder ignores `10 0B 76` |
| `showConfirmBoxWithOffsets` | confirm box | ❌ |
| `setText` | short radio text line | ❌ |
| `setTime` / `setState` | clock / power (OFF=clock+outside temp) | ❌ (not screens) |

## Guardrails (PROTOCOL-ADJACENT — do not break a real panel)
- Frame builders ARE the protocol: change `showInfoMenu`/`showConfirmBoxWithOffsets`
  bytes ONLY to match a real capture; verify the emitted `@TX` against the `.canlog`
  AND on the panel. A green build is NOT sufficient.
- `@TX` wire format is FROZEN (`WireProto::emitTx`) — proxy + WS depend on it.
- Don't touch: `affa3_do_send` ISO-TP/ACK loop, `busAlive` gate, the 2 s per-frame ACK,
  `skip_funcreg` semantics. DisplayTransport only SELECTS sinks, never rewrites frames.
- Don't push from the agent (credential prompt blocked) — the user pushes.

## Fast commands
- Flash mini: `pio run -e esp32dev-mini -t upload --upload-port COM6`
- Build/test: `pio run -e esp32dev` · `pio test -e native`
- Free a COM port: stop `serial_proxy.py` (it holds it) before flashing/monitor.
- Serial resets the board (DTR/RTS) on open — that explains "reboots" at tool switches,
  not firmware crashes. `/wire` (WiFi) + a USB monitor can run together (different links).
