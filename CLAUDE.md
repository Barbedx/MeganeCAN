# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based CAN-bus companion for Renault Mégane 2 infotainment/multimedia displays. The firmware connects an ESP32-C3 to the vehicle CAN bus and communicates with the OEM "AFFA3" display unit via CAN frames, bridging phone media controls (Apple Media Service over BLE), providing OBD/ELM327 diagnostics via a WiFi ELM327 adapter, and exposing a web UI for configuration and OTA updates.

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

Two envs: **`esp32dev-mini`** (ESP32-C3 SuperMini — the car board, native USB) and **`esp32dev`**
(classic ESP32 WROOM — the bench board, CP210x UART). Build/flash a specific board and repo path:
`pio run -e esp32dev -t upload --upload-port COMx -d <repo>`. CAN pins: RX=GPIO3, TX=GPIO4.
If BLE bonds won't persist on a board, `pio run -e <env> -t erase` first (corrupt NVS).

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

### CAN bus emulator (PC-side virtual display)

`CanUtils::sendFrame()` mirrors every outbound frame to serial as `@TX <id> <bytes>` (before the
live-bus gate, so frames are captured even when bench TX is suppressed). A PC tool decodes the
AFFA3 stream and renders a virtual display; injecting `(id | 0x400)` ACKs back over serial closes
the loop so the bench runs without the 2s no-display timeouts. Live serial is shared with the
developer via `tools/serial_proxy.py` (SSE at `http://localhost:8080`). Goal: reverse-engineer the
AFFA3NAV navigation screen to drive turn arrows from iPhone ANCS/Maps.

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
