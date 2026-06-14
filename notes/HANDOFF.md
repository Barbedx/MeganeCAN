# MeganeCAN — BLE/AMS/ANCS integration handoff

A clean-slate context dump: goals, what's done, every hard-won lesson, the toolchain,
current state, and next steps. Read this first.

## TL;DR
We replaced MeganeCAN's broken **central-mode** BLE (it scanned for an iPhone advertising
Spotify's service UUID — Spotify stopped advertising it) with a **peripheral-mode** AMS/ANCS/CTS
client (pair from iOS Settings, bonding, `NimBLEServer::getClient`). Added WiFiManager, a
redesigned web dashboard, a RAM event log + a configurable CAN log (both downloadable over WiFi).
All committed to local `main` (3 commits) **+ one uncommitted fix** (advertising). Reference
sandbox lives at `../NimBLE-AMS-CNTRL-01` (published).

**Last action:** fixed a BLE advertising 31-byte overflow that silently dropped the AMS
solicitation (so fresh iPhones never saw us as an AMS accessory → "Apple music service not found").
Fix flashed to the WROOM bench board; **awaiting a fresh-pair test** (see Next steps).

## Goals
ESP32 in a Renault Mégane: read CAN, drive the factory **Carminat/AFFA3 display**, OBD via
**ELM327** (currently disabled — unstable), web UI + OTA. The BLE part: show iPhone now-playing
(AMS) and notifications (ANCS) on the car screen + steering-wheel media control; set the clock
(CTS). Port the proven approach from the sandbox. Long term it stays AMS-only for media; a parked
idea is steering-wheel control via SWC (resistor ladder / MCP4251) instead of AMS.

## Two repos
- **`c:\Users\Andru\source\repos\MeganeCAN`** — the real product (this repo). GitHub
  `Barbedx/MeganeCAN`. Default branch **`main`** (NOT master). Local `main` is **3 commits ahead**
  of origin; **push is blocked** (`andriipetruk-hue` gets 403 — needs the `Barbedx` account).
  Work was done on branch `feature/ble-peripheral-ams-ancs-wifimanager`, fast-forward-merged to main.
- **`c:\Users\Andru\source\repos\NimBLE-AMS-CNTRL-01`** — the standalone sandbox/reference
  (published "for researchers"). Has its own README + CLAUDE.md documenting the approach. Memory
  files for that project also capture the architecture.

## Hardware & toolchain (how to actually work)
- PlatformIO CLI: `~/.platformio/penv/Scripts/pio.exe`. **Always pass `-d <repo path>`** (the shell
  cwd drifts). Git Bash environment on Windows.
- Envs (in `platformio.ini`):
  - **`esp32dev-mini`** — ESP32‑C3 SuperMini (native USB-Serial/JTAG, auto-reset works).
  - **`esp32dev`** — classic ESP32 **WROOM** devboard (CP210x UART bridge). Flash ~91% (bigger than C3).
- Commands:
  - Build: `pio.exe run -e <env> -d <repo>`
  - Flash: `pio.exe run -e <env> -t upload --upload-port COMx -d <repo>`
  - Monitor (run in background, log to file): `pio.exe device monitor -p COMx -b 115200 --filter direct -d <repo>`
    then read the logfile. The serial event log is also downloadable over WiFi (see below).
- Boards/ports seen this session:
  - C3 SuperMini was **COM6**. The **car** has a C3. A spare local C3 stopped enumerating
    (no COM port even with another cable → suspected dead board or charge-only cable; red LED = power only).
  - WROOM bench board = **COM5** (Silicon Labs CP210x). Auto-reset works on it.
  - Detect ports with PowerShell: `[System.IO.Ports.SerialPort]::GetPortNames()` (more reliable than `pio device list`).
  - Some classic boards lack working auto-reset → enter download mode manually (hold BOOT, tap EN, release BOOT).
- A **CAN transceiver module pulled from the car** can be wired to the C3 (RX=GPIO3, TX=GPIO4). On a
  bare bench board without it, CAN `affa3_send(): timeout` is expected (no bus to ACK).

## Architecture (current)
Peripheral model: ESP32 advertises as **"MeganeCAN"**; the iPhone connects from **Settings →
Bluetooth**; on connect we take a GATT *client* over that inbound connection
(`NimBLEServer::getClient(connInfo)`) and read AMS + ANCS + CTS, which the phone hosts. Bonding →
silent auto-reconnect.

Modules (each a `namespace`, header+impl):
- `src/bluetooth.{h,cpp}` — BLE peripheral link: advertising, bonding, connection lifecycle,
  per-service bring-up + retry, re-advertise while disconnected. `GetStatusText/GetStatusJson`.
- `src/apple_media_service.{h,cpp}` — AMS decode (`MediaInformation`, with `mLastPlaybackInfoMs`
  for elapsed extrapolation) + remote commands.
- `src/apple_notification_service.{h,cpp}` — ANCS history (UID-keyed, pruned on Removed),
  `Process()` does deferred Control Point writes from the loop, `CategoryName()`, `AppName()`.
- `src/current_time_service.{h,cpp}` — CTS → `settimeofday()`.
- `src/wifi_manager.{h,cpp}` — STA (NVS creds, optional static IP, mDNS `meganecan.local`) → AP
  fallback (`ESP32_MeganeCan_AP`, captive DNS). mDNS only in STA. `setSleep(true)` for coexistence.
- `src/server/HttpServerManager.cpp` — PsychicHttp dashboard + all `/api/*`. Existing display/ELM/OTA
  routes kept; added BT/media/notifs/cmd/wifi/log/can endpoints + a redesigned card UI.
- `src/utils/Log.{h,cpp}` — RAM ring-buffer event log (mirrors Serial), `/api/log`.
- `src/utils/CanLog.{h,cpp}` — configurable CAN logger (ID allow-list + live seen-ID counts),
  NVS-persisted, `/api/can/log`.
- `src/display/Carminat/CarminatDisplay.cpp` — media screen + ANCS notification popup (`tickMedia`).
- `src/main.cpp` — wiring. Init order: BLE (background task) → WiFiManager → server → display → CAN.
  Display type read from NVS `config/display_type` (use **`carminat`** for the Carminat display;
  an old value `affa3nav` falls back to UpdateListBase!). BT mode `config/bt_mode` = `ams` | `keyboard`.

## Hard-won lessons / invariants (do not regress)
1. **Advertising must fit 31 bytes.** flags(3) + name + 128‑bit AMS solicitation(18). "MeganeCAN"
   name(11) + 3 + 18 = 32 → overflow → `addData` silently drops the solicitation → iOS doesn't see
   an AMS accessory → fresh pair gets "Apple music service not found". **Current fix:** name in the
   PRIMARY packet, solicitation (AD `0x15`) in the **SCAN RESPONSE** (iOS reads solicited UUIDs from
   combined active-scan). Verified sizes logged: `adv=14, scanrsp=18`. (Alt fix: shorten the name to ≤8 chars.)
   Note: `NimBLEAdvertisementData::addData()` does NOT prepend the AD length byte — build `[0x11][0x15][16 UUID LE]`.
2. **iOS exposes AMS/ANCS only to a bonded peer**, and only properly when we solicited AMS. After a
   bond exists, AMS is available regardless of current advertising — which is why a board with a
   *pre-existing* bond "works" even if advertising is broken (this masked the bug on the C3; a fresh
   WROOM exposed it).
3. **First-pair dance:** iOS connects, bonds, then disconnects (reason 0x16/0x13) and reconnects;
   AMS is exposed on the bonded reconnect. So we MUST keep advertising while disconnected
   (`bluetooth.cpp` re-advertises every 3s; `advertiseOnDisconnect(true)` alone wasn't enough).
   If the first connection is dropped before the bond is finalized, the bond doesn't persist (saw `bonds stored: 0`).
4. **secureConnection() BEFORE service discovery**, and bring up AMS/ANCS/CTS **independently, each
   retried** until iOS exposes it (they appear with different timing; don't stop after AMS).
5. **BLE notification callbacks must never block / never do a GATT write-with-response** → deadlocks
   the NimBLE host (buffers exhaust: `Failed to allocate buffer, retrying`). ANCS queues the UID in
   the callback; `Process()` (loop task) does the Control Point write.
6. **WiFi+BLE coexistence:** `WiFi.setSleep(true)` (modem sleep) — without it the BLE link gets
   supervision-timeout churn (disconnect `reason=520`).
7. **mDNS only in STA mode.** In AP mode it floods `WiFiUdp endPacket: could not send data: 12`
   (ENOMEM); AP clients use `192.168.4.1` anyway.
8. **Carminat display can't render UTF-8** → run text through `transliterateToAscii()` (exists in
   `utils/TextUtils.cpp`; Ukrainian/Russian/Polish Cyrillic → Latin, emoji → `?`). Raw UTF-8 = mojibake.
9. **`AppName(bundleId)`** maps app ids to friendly names ("ph.telegra.Telegraph" → "Telegram") so the
   notification screen shows the source app, not the generic category. ANCS log line records
   `cat=.. app=.. | title: msg` (raw UTF-8 — readable in the browser) to grow the mapping from real data.
10. Stale iOS bonds: a different physical chip advertising the same name "MeganeCAN" → iOS's saved
    bond (from another board) fails ("connection failed" in scanner apps). Forget it on the iPhone.

## Web dashboard & logs (debugging without USB)
- AP fallback `ESP32_MeganeCan_AP` → `http://192.168.4.1`; or STA → `http://meganecan.local`.
- Dashboard: **Now Playing** + **Notifications** cards up top; collapsible sections for Bluetooth,
  WiFi (scan + creds), Display & text, Device settings (restart), OBD/ELM, **CAN logging**, System.
- `/api/log` (+ System → "Download log") — RAM event log (BLE/WiFi/ANCS, timestamped).
- **CAN logging** section: enable + hex ID allow-list; "Seen IDs (tap to add to filter)" shows live
  IDs+counts; **Download CAN log** = `/api/can/log`. Persisted in NVS. Use it to find e.g. the radio's
  image frame ID then filter to it.
- OTA: `/update` (ElegantOTA, psychic-http). Firmware bin: `.pio/build/<env>/firmware.bin`.

## Git state
- Local `main`: `8c56310` (origin) → `f7681a5` (peripheral BLE+WiFiManager) → `fb4d62a` (dashboard
  redesign + RAM/CAN logging + coexistence/retry fixes) → `3fec72f` (notification screen: app source,
  transliterate, richer log). **Plus uncommitted:** mDNS-AP gate, re-advertise-while-disconnected,
  and the advertising primary/scanresp split. **Commit these and push** (with an account that has
  access to `Barbedx/MeganeCAN`).

## Immediate next steps
1. Commit the uncommitted fixes (advertising split + mDNS gate + re-advertise) to `main`.
2. **Fresh-pair test** on the WROOM (COM5): on iPhone Forget any stale "MeganeCAN", then Settings →
   Bluetooth → it should now appear (AMS solicitation finally advertised) → Pair. Watch serial for
   `Phone connected → Auth complete bonded=1 → AMS started → ANCS started → CTS started` and confirm
   `bonds stored` becomes ≥1 on the next boot. Download `/api/log` if it misbehaves.
3. If it still fails: try shortening the advertised name to ≤8 chars (put name + solicitation both in
   the primary packet, like the sandbox's working "CTRL 01").
4. Car testing of the Carminat rendering: notification popup (app source + transliterated text +
   message across 2 rows), media screen, steering keys. Collect `/api/log` ANCS combos to extend `AppName`.
5. Push to GitHub from the right account.

## Parked ideas
- **SWC** (steering-wheel control) emulation via resistor ladder / MCP4251 as an alternative to AMS commands.
- **Android / BLE HID keyboard** (`BleMediaKeyboard.h`, `bt_mode=keyboard`) — Android has no AMS/ANCS.
- **ELM327/OBD** — disabled (`elm_enabled=false`, unstable). When re-enabled, its WiFi STA (to the
  "V-LINK" adapter) is mutually exclusive with home-WiFi STA on one radio — reconcile then.
