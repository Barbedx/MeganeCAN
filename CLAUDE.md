# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based CAN-bus companion for Renault Mégane 2 infotainment/multimedia displays. The firmware connects an ESP32-C3 to the vehicle CAN bus and communicates with the OEM "AFFA3" display unit via CAN frames, bridging phone media controls (Apple Media Service over BLE), providing OBD/ELM327 diagnostics via a WiFi ELM327 adapter, and exposing a web UI for configuration and OTA updates.

**ACTIVE REDESIGN — read `notes/ARCHITECTURE-V2.md` before any new work.** The OEM radio was
replaced by a DUDU7 Android head unit; the project is a dual-board gateway. **V2.1 FINAL role
split: the ESP32-WROVER is the GW board (brain — vehicle CAN, DUDU canbox UART, BLE, WiFi/web),
this C3 becomes the DISP board (display co-processor — multimedia CAN + AFFA3 drivers + USB
bench proxy), linked over LinkProto UART.** Migration phases M1–M4 in §9 (P0/P1 code-complete;
P1 bench validation of the physical link comes before the M-phase code swap). The document is
self-contained (research annex: Raise bytes, Mégane II CAN IDs, DUDU7 facts); raw dumps in
`notes/research/`. Current phase and acceptance criteria are in its §9.

## Build & Flash (PlatformIO)

```bash
# Build firmware
pio run

# Upload over USB
pio run -t upload

# Serial monitor (115200 baud)
pio device monitor

# OTA upload (requires device on same network)
pio run -t upload --upload-port <device-ip>
```

Firmware envs (V2.2 final pair + transitional):
- **`gw-wrover`** — GW brain image (WROVER): BLE+WiFi/web+routers+vehicle CAN+canbox+link;
  display is remote (`src/gw/RemoteDisplay` → DISP_CMD/MEDIA_TEXT). partitions_gw.csv.
- **`disp-c3`** — DISP thin IO Controller (C3): display drivers + multimedia CAN + SWC→KEY_EVT
  + DISP_CMD server + `@PROG` USB↔link bridge (`tools/flash_gw.py` flashes GW through it).
- **`esp32dev-mini`** (legacy full C3 image, still the in-car firmware until M4 cutover),
  **`esp32dev`** (bench WROOM, full image for display-RE), **`vehicle-wrover`** (P1 VH-only
  image, subsumed by gw-wrover).
Build/flash: `pio run -e <env> -t upload --upload-port COMx -d <repo>`. Multimedia CAN pins:
RX=GPIO3, TX=GPIO4. If BLE bonds won't persist on a board, `pio run -e <env> -t erase` first
(corrupt NVS). `pio test -e native` runs the host suites (incl. `test_link`, `test_vh`,
`test_media`, `test_keys`).

P0/P1 layer map: `src/media/` (MediaInfo/IMediaSource/MediaRouter + AMS/HU sources), `src/keys/`
(KeyRouter + AMS/HID/HU-UART sinks, packed `key_sinks` config), `src/link/` (LinkProto: COBS+CRC16
codec, LinkPort; `link/mm/MmLinkService` = MM-side endpoint + HTTP mailbox), `src/vh/` (VehicleDbc
decode table, VehicleDecoder, Raise CanboxEmitter, HuRxParser; `vh/target/` = VH-only firmware:
VhConfig, FsLogger, LinkTunnel, vh_main). Web page `data/vh.html` (`/vh`) drives VH capture,
config, log download and firmware OTA through `/api/vh*`.

## Secrets

`src/secrets.h` is intentionally untracked (`git update-index --assume-unchanged src/secrets.h`). It defines:
- `Soft_AP_WIFI_SSID` — soft AP name for the web UI
- `Soft_AP_WIFI_PASS` — soft AP password

## Role in the car (the big picture)

The ESP32 sits on the car CAN bus **between the head-unit radio and the dashboard display**, and
can **emulate the radio** to drive the display directly — that is the point of the project (augment
or replace the OEM radio). To replace the radio it must present itself to the display *as* the
radio: the **function-registration** handshake (see `skip_funcreg` below).

### Radio protocols × displays (validated matrix)

Two physical displays — **monochrome** (large, graphical: menus, media, notifications) and
**segment** (text-only) — and two radio protocols the firmware emulates:

| Radio protocol | Monochrome display | Segment display |
|---|---|---|
| **Carminat** (`CarminatDisplay`) | ✅ only this | ❌ |
| **UpdateList** | ✅ `UpdateListMenuDisplay` (`updatelist_menu`) | ✅ `UpdateListDisplay` (`updatelist`, 8-seg) |

The *real* monochrome display identifies the connecting radio during its registration handshake;
our firmware does **not** auto-detect — `display_type` (NVS) statically selects the driver
(`carminat` / `updatelist` / `updatelist_menu` / fallback `UpdateListBase`).

### Display abstraction

`IDisplay` (pure interface) → `AffaDisplayBase` (shared CAN/ISO-TP send + sync state machine) →
`CarminatDisplay`, `UpdateListBase`, `UpdateListDisplay` (8-seg), `UpdateListMenuDisplay` (mono LCD).
`initDisplay()` in `main.cpp` builds the driver from NVS; the global `AffaDisplayBase *display` is
used by `loop()` (`tick`/`tickMedia`/`processEvents`), `gotFrame()` (`recv`), the AMS callback
(`setMediaInfo`), and HTTP routes (`/emulate/key`, `showMenu`, …).

### CAN / AFFA3 wire protocol

Inbound frames arrive via `gotFrame()` (`CAN0.setGeneralCallback`) → `display->recv()`. Outbound
goes through `affa3_send()` → `affa3_do_send()` → `CanUtils::sendFrame()` → `CAN0.sendFrame()`.

- **ISO-TP framing:** first frame = 7 raw bytes; consecutive = `[0x20+N][6 bytes]`; padded with the
  protocol filler (Carminat `0x00`, UpdateList `0x81`).
- **Per-frame ACK:** `affa3_do_send` blocks ≤2s waiting for a reply on `(sentID | 0x400)`:
  `[0x74…]`=DONE, `[0x30 0x01 0x00…]`=PARTIAL, else ERROR.
- **CAN IDs:** Carminat sync `0x3AF`/reply `0x3CF`, ctrl+text `0x151`, keys `0x1C1`.
  UpdateList sync `0x3DF`/reply `0x3CF`, ctrl `0x1B1`, text `0x121`, keys `0x0A9`.
- **Registration:** radio→`0x3CF [0x61 0x11]` → ESP replies `0x70…` (Carminat also 2×`0xB0`);
  keepalive `0xB9`/`0x79`, peer-alive `0x69`.

**`skip_funcreg` (NVS `config`):** `false` = ESP acts as the radio and runs the registration
handshake (replace-radio mode); `true` = a real radio is present and owns registration, ESP is
passive (no sync, no auto-reply). Auto-detect of the radio is impossible while passive — hence
`display_type` is manual.

### CAN bus emulator (PC-side virtual display) — working

The bench has no real display, so a **PC-side virtual Carminat** in `tools/serial_proxy.py` decodes
the real AFFA3 frames (never the human debug prints — see Claude memory). Pieces:
- `CanUtils::sendFrame()` mirrors every outbound frame to serial as **`@TX <id> <bytes>`** via
  `WireProto.h` (the one UART contract: `@TX`/`@RX`/`@EV` fw→PC, `@KEY`/`@INJ` PC→fw), before the
  live-bus gate so frames emit even when bench TX is suppressed.
- **Self-ACK** (`AffaDisplayBase::_emuSelfAck`, toggle **`GET /api/emu?on=1`**): with no real display
  to answer `affa3_do_send`'s per-frame ACK, the sender acks its own frames so the COMPLETE
  multi-frame AFFA3 sequence emits. Wire bytes are identical to a real send.
- The proxy (`http://localhost:8080`, SSE) reassembles ISO-TP (frame0 `0x10` = 8 bytes; consecutive
  `0x2N` append bytes[1..7]) and renders the screen. **All screens are `showMenu` over `0x151`** —
  menu, now-playing, and ANCS notification popups — so one decoder covers all (payload: scrollLock
  [10], header[11..36], item1 marker[38]+text[39..63], item2 marker[65]+text[66..95]; highlight =
  single `07 29 01 <rowId>`).
- Drive the media screen on the bench: `/api/emu?on=1` + `/setaux` + music playing + menu closed
  (Load-hold `/emulate/key key=0 hold=1` toggles the menu). `showMenu` transliterates all text to
  ASCII so Cyrillic doesn't mojibake.
- `@INJ <id> <bytes>` injects a CAN RX frame (a future *true* closed loop ACKs on `id|0x400`); note
  serial INPUT from the proxy doesn't reach SerialCommands yet — use the HTTP routes.

Goal: reverse-engineer the AFFA3NAV navigation screen to drive turn arrows from iPhone ANCS/Maps.

### Bench vs car (CAN safety)

A bare bench board has **no CAN transceiver**. Transmitting onto a bus that never ACKs drives the
TWAI controller bus-off, and the esp32_can watchdog's auto-recovery then asserts (`twai.c:184
tx_msg_count`) into a reboot loop. `CanUtils::busAlive()` gates TX on received traffic — no RX
(bench) → TX suppressed; a live bus (car) → TX within milliseconds. No build flag needed.

### Bluetooth (peripheral AMS/ANCS — NOT central)

The ESP advertises as a **BLE peripheral**; the iPhone connects from Settings → Bluetooth and the
ESP takes a GATT *client* over that inbound link (`NimBLEServer::getClient`). It reads AMS
(now-playing + remote commands), ANCS (notifications), CTS (clock). See `src/bluetooth.cpp`.
Hard-won invariants:
- Advertise the name **and** the AMS solicitation in the **primary** packet (≤31 bytes → name must
  be short); a scan-response-only solicitation is not surfaced to a fresh iPhone.
- BLE notification callbacks must never block / never do a GATT write-with-response (deadlocks the
  host); defer writes to the loop.
- Bonds must persist (`getNumBonds()>0`); if they don't on a given board, the NVS is corrupt — a
  full `erase_flash` fixes it (not code).

### WiFi + memory discipline (the device is RAM-tight)

WiFiManager: STA (NVS creds) → AP fallback; mDNS `meganecan.local` (STA only). Heap is tight with
BLE+WiFi+HTTP+AMS all live (~62KB free / ~45KB largest contiguous block). Keep it stable:
- `WiFi.setSleep(true)` **and** relax the BLE connection on connect
  (`updateConnParams` ~30–50ms interval + slave latency) or the dashboard is starved.
- PsychicHttp: `lru_purge_enable=true`, small `max_open_sockets`; don't auto-scan WiFi from the
  dashboard; gate scans on the largest *contiguous* block (`getMaxAllocHeap`).
- Prefer fixed `char[]`/streaming over `String`/`std::string` churn in HTTP responses; the RAM ring
  log was removed (use the serial proxy). A loop heap watchdog logs `[heap] free/min/maxblk`.
- Config is cached in RAM at boot (`utils/AppConfig`) — getters read it, not NVS (stops per-request
  NVS churn + `nvs_open NOT_FOUND` spam); config setters `ESP.restart()` so the cache reloads.
- The dashboard polls **one** `/api/dashboard` (media+notifs+bt+wifi+can) instead of 5 endpoints, to
  hold fewer keep-alive sockets. Open follow-ups (see `notes/HANDOFF.md`): stream JSON, serve the
  silence the `affa3_do_send` debug spam during continuous media renders.

### Web UI pages (`data/*.html` → gzipped into the app image)

The five pages (`dashboard`, `wire`, `preview`, `diag`, `affa3test`) live as plain files in
`data/`. `gen-pages.py` (a `pre:` extra_script) gzips them at build time into
`$BUILD_DIR/generated/GeneratedPages.h` as byte arrays; `servePage()` in `HttpServerManager.cpp`
sends them straight from flash with `Content-Encoding: gzip`. 33KB of HTML → 13KB of flash, no
runtime decompression, no copy. Edit the `.html` files — never the generated header.

**Serving them from LittleFS was tried and rejected, with numbers.** Moving the pages out of the
app image saved 43KB in `HttpServerManager.cpp.o`, but pulling in `esp_littlefs` + `vfs` + the
`LittleFS` wrapper cost **58KB** — nothing else in the firmware uses a filesystem, so the whole
stack was new weight. Net **+14.5KB, i.e. worse**, plus a second flash artifact and a way to
brick the UI (app OTA'd without the FS image). Don't redo it unless something else earns the
filesystem first.

### Flash budget

`partitions_ota.csv` gives each OTA slot 1.375MB, so the "% used" in a build is against that, not
the 4MB chip. Two measured levers, both already applied:
- **`-fno-exceptions`** (both envs) — saves ~72KB. The project had exactly one `throw`, an
  unimplemented `setTextBig` stub nobody caught (on target that is an abort + reboot, not a
  diagnostic). Everything built from source loses its unwind tables; the prebuilt IDF blobs
  (`net80211`, `lwip`, `btdm`) keep theirs, which is the remaining ~38KB.
- **gzipped pages** (above) — saves ~20KB.

For reference, where the image actually goes: WiFi/networking ~435KB, NimBLE 128KB,
`libbtdm_app` (BLE controller blob) 64KB. NimBLE is ~10% — it is not the thing to cut.

### ELM327 / OBD Diagnostics

`MyELMManager` connects via TCP WiFi to a "V-LINK" ELM327 adapter (IP `192.168.0.10:35000`). It cycles through a combined `PidPlan` (querying ECU headers 7E0, 743, 744, 745, 74D) using a non-blocking state machine (`tick()` called from `loop()`). Results are cached in `valueCache` (keyed by `shortName`) and exposed as JSON via `snapshotJson()`.

PID plans live in `src/ElmManager/PidPlan_*.h`. Each plan defines `MetricDef` entries with letter-indexed byte extraction helpers (`U8`, `U16`, `getBIT` from `DiagPlanCommon.h`).

### HTTP Server & Web UI

`HttpServerManager` wraps PsychicHttp. It holds references to `IDisplay` and `Preferences`, and optionally an `MyELMManager*` (attached via `attachElm()`). Routes serve the configuration UI and OTA (via ElegantOTA).

`DisplayCommands::Manager` (`src/commands/DisplayCommands.*`) translates HTTP requests into `IDisplay` calls.

### WiFi (see "WiFi + memory discipline" above)

`WiFiManager` owns networking: STA with NVS-persisted home creds → AP fallback (`ESP32_MeganeCan_AP`,
secrets.h) for config; mDNS only in STA. (ELM327's separate STA to the "V-LINK" adapter is mutually
exclusive with home STA on one radio — reconcile if ELM is re-enabled; it's disabled by default.)

### Serial Commands

`SerialCommands` processes commands over USB serial:
- `e` / `d` — enable/disable display
- `st HHMM` — set time
- `msr <text> [delay_ms]` — scroll right
- `msl <text> [delay_ms]` — scroll left

### NVS Persistence

`Preferences` namespaces used:
- `"config"` — `display_type` (`"carminat"` | `"updatelist"` | `"updatelist_menu"`), `bt_mode`
  (`"ams"` | `"keyboard"`), `auto_time`, `elm_enabled`, `skip_funcreg`
- `"display"` — `autoRestore` (bool), `lastText` (string), `welcomeText` (string)

### Key Data Types

- `AffaCommon::AffaKey` — enum for remote button events (src/display/AffaCommonConstants.h)
- `AffaCommon::SyncStatus` — bitmask tracking display sync handshake state
- `PidPlan` / `MetricDef` — OBD query definition (src/ElmManager/DiagPlanCommon.h)
- `MenuItem` / `Field` — nav menu item model (src/display/Affa3Nav/Menu/)

### merge-bin.py

Post-build script (referenced via `extra_scripts`) that merges bootloader, partition table, and app binaries into a single flashable `.bin` for distribution.
