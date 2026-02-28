# MeganeCAN

ESP32-based CAN-bus companion for Renault Megane 2 infotainment displays.

Connects an **ESP32** to the car CAN bus and acts as a smart sidecar: renders custom menus and text on the OEM display, bridges phone media controls via Bluetooth (Apple Media Service or BLE Keyboard), exposes a web UI for live configuration, and supports OTA firmware updates. Optional ELM327/OBD diagnostics via WiFi.

🇺🇦 When you use this project, you automatically support Ukraine.

> ⚠️ Safety: hobby project for off-road / testing use. Do not interact with menus while driving.

---

## Features

| Category | Details |
|---|---|
| **CAN bus** | 500 Kbps, GPIO 3 (RX) / GPIO 4 (TX). Listens to OEM frames and replies to sync/ack. |
| **Display** | Drives Affa2 (8-segment or full-LED) and Affa3Nav (Carminat) OEM displays via ISO-TP style CAN. |
| **Bluetooth** | AMS mode: pairs as BLE central to iPhone, reads now-playing info, syncs clock. Keyboard mode: BLE HID peripheral. |
| **Web UI** | PsychicHttp async server on port 80. Configure display type, BT mode, text, time. |
| **OTA** | ElegantOTA at `/update`. |
| **Diagnostics** | ELM327 over WiFi STA (V-LINK adapter). Queries ECUs and exposes JSON at `/api/live`. |

---

## Hardware

Minimum:
- **ESP32-C3** (or ESP32) development board
- **CAN transceiver** — SN65HVD230 / TJA1050 / MCP2551
- Wiring to CAN-H, CAN-L, GND, and a fused 5 V supply

> Provide a **dedicated power supply** to the ESP32 — drawing from the display bus directly can cause resets under load.
> Do **not** add a second 120 Ω terminator if the bus is already terminated.

Pinout (ESP32-C3):

| Signal | GPIO |
|---|---|
| CAN RX | 3 |
| CAN TX | 4 |

---

## Architecture

### Class diagram

```
IDisplay  (pure virtual interface)
│  tick(), recv(), processEvents()
│  setText(), setState(), setTime()
│  showMenu(), ProcessKey(), onKeyPressed()
│
└── AffaDisplayBase  (abstract — ISO-TP CAN send engine)
      │  affa3_send() / affa3_do_send()
      │  funcs[] array  (registered CAN function IDs + ack state)
      │  _sync_status   (FAILED | PEER_ALIVE | START | FUNCSREG)
      │  setMediaInfo()  — virtual no-op, overridden by subclasses
      │  tickMedia()     — virtual no-op, overridden by subclasses
      │  setKeyHandler() — installs the key callback
      │
      ├── Affa2Base  (Affa2 CAN protocol — basic text, fallback default)
      │     funcs: { 0x121 SETTEXT, 0x1B1 DISPLAY_CTRL }
      │     sync ping: 0x3DF   filler: 0x81
      │     │
      │     ├── Affa2Display      (8-segment — adds media title scroll)
      │     │     + setMediaInfo()  builds "Artist - Title" string
      │     │     + tickMedia()     scrolls 8-char window every 400 ms
      │     │
      │     └── Affa2MenuDisplay  (full-LED — stub, same CAN protocol)
      │
      └── Affa3NavDisplay  (Affa3Nav "Carminat" — full nav display)
            funcs: { 0x151 SETTEXT/CTRL, 0x1F1 NAV }
            sync ping: 0x3AF   filler: 0x00
            + showMenu() / highlightItem() / showInfoMenu()
            + setMediaInfo() — renders full media screen
            + tickMedia()    — scrolls title (18-char window)
            + Menu system (mainMenu, key handling, NVS-backed BT items)
```

### Main loop data flow

```
 ┌──────────┐   CAN frame   ┌──────────────────┐
 │ CAN bus  │ ────────────► │ gotFrame()        │
 └──────────┘               │ → display->recv() │
                            └──────────────────┘

 ┌───────────────┐  MediaInformation  ┌─────────────────────────┐
 │ BLE / AMS     │ ─────────────────► │ onDataUpdateCallback()  │
 │ (iPhone)      │                    │ → display->setMediaInfo()│
 └───────────────┘                    └─────────────────────────┘

 loop()
   ├── serialCommands.ReadSerial()
   ├── ElegantOTA.loop()
   ├── Bluetooth::Service()          (AMS mode)
   ├── display->tickMedia()          (non-blocking scroll)
   ├── auto-time sync via CTS        (AMS mode, once per connection)
   ├── display->processEvents()
   └── elmManager->tick()            (OBD, when WiFi connected)
```

### BT key mapping

| Physical key | AMS action | BLE Keyboard HID |
|---|---|---|
| Pause | TogglePlayPause | KEY_MEDIA_PLAY_PAUSE |
| RollUp | NextTrack | KEY_MEDIA_NEXT_TRACK |
| RollDown | PreviousTrack | KEY_MEDIA_PREVIOUS_TRACK |
| VolumeUp (hold) | VolumeUp ×15 | KEY_MEDIA_VOLUME_UP |

---

## CAN packet IDs

### Affa2 protocol (Affa2Base / Affa2Display / Affa2MenuDisplay)

| Constant | ID | Direction | Purpose |
|---|---|---|---|
| `PACKET_ID_SYNC` | `0x3DF` | ESP32 → display | Alive ping |
| `PACKET_ID_SYNC_REPLY` | `0x3CF` | display → ESP32 | Sync ack |
| `PACKET_ID_SETTEXT` | `0x121` | ESP32 → display | Set text |
| `PACKET_ID_DISPLAY_CTRL` | `0x1B1` | ESP32 → display | Enable (0x02) / Disable (0x00) |
| `PACKET_ID_KEYPRESSED` | `0x0A9` | display → ESP32 | Key press event |
| `PACKET_REPLY_FLAG` | `0x400` | — | OR-mask on ID for ack frames |
| `PACKET_FILLER` | `0x81` | — | Padding byte |

### Affa3Nav (Carminat)

| Constant | ID | Direction | Purpose |
|---|---|---|---|
| `PACKET_ID_SYNC` | `0x3AF` | ESP32 → display | Alive ping |
| `PACKET_ID_SYNC_REPLY` | `0x3CF` | display → ESP32 | Sync ack |
| `PACKET_ID_SETTEXT` / `DISPLAY_CTRL` | `0x151` | ESP32 → display | Text + enable/disable (same ID, different payload) |
| `PACKET_ID_NAV` | `0x1F1` | ESP32 → display | Menu / nav data |
| `PACKET_ID_KEYPRESSED` | `0x1C1` | display → ESP32 | Key press event |
| `PACKET_FILLER` | `0x00` | — | Padding byte |

Scroll lock indicator byte (Affa3Nav `showMenu`):

| Value | Meaning |
|---|---|
| `0x00` | No scroll arrows |
| `0x07` | Scroll up arrow |
| `0x0B` | Scroll down arrow |
| `0x0C` | Both arrows |

---

## Display types

Selected via NVS key `config/display_type`. Takes effect after restart.

| NVS value | Class | Notes |
|---|---|---|
| `affa2` | `Affa2Display` | Affa2 8-segment LED — extends Affa3Display; scrolls "Artist - Title" (8-char window, 400 ms/step) via AMS |
| `affa2menu` | `Affa2MenuDisplay` | Affa2 full-LED — extends Affa3Display; same CAN protocol, stub for future menu extension |
| `affa3` | `Affa2Base` | Affa2 protocol base — basic text only; used as fallback for unknown display_type values |
| `affa3nav` *(default)* | `Affa3NavDisplay` | Affa3Nav "Carminat" — full media screen + interactive menu system |

---

## NVS configuration keys

| Namespace | Key | Type | Default | Values |
|---|---|---|---|---|
| `config` | `display_type` | String | `affa3nav` | `affa2` / `affa2menu` / `affa3` / `affa3nav` |
| `config` | `bt_mode` | String | `ams` | `ams` / `keyboard` |
| `config` | `auto_time` | Bool | `true` | Sync display clock from phone via CTS |
| `display` | `autoRestore` | Bool | `false` | Restore last text on boot |
| `display` | `lastText` | String | — | Last text shown (restored on boot) |
| `display` | `welcomeText` | String | — | Scroll on boot (overrides default) |

---

## Build & flash

```bash
# Build
pio run

# Flash over USB
pio run -t upload

# Serial monitor (115200 baud)
pio device monitor

# Build + flash in one step
pio run -t upload && pio device monitor
```

Target: `esp32-c3-devkitm-1` (see `platformio.ini`).

### WiFi / secrets

Edit `src/secrets.h` (not tracked by git):

```cpp
#define Soft_AP_WIFI_SSID "MeganeCAN"
#define Soft_AP_WIFI_PASS "yourpassword"
```

The file is ignored via:

```bash
git update-index --assume-unchanged src/secrets.h
```

---

## Web UI (`http://<device-ip>/`)

| Route | Method | Purpose |
|---|---|---|
| `/` | GET | Main configuration page |
| `/setDisplay` | POST | Set display type, restarts ESP32 |
| `/setbtmode` | POST | Set BT mode + auto_time, restarts ESP32 |
| `/config/restore` | GET | Read (`?enable` absent) or write (`?enable=1/0`) auto-restore flag |
| `/static` | GET | Show static text (`?text=`, `?save` to persist) |
| `/scroll` | GET | Scroll text (`?text=`, `?save` to save as welcome) |
| `/settime` | GET | Set display clock (`?time=HHMM`) |
| `/setVoltage` | GET | Debug voltage override |
| `/getdisplaytype` | GET | Returns current NVS display_type string |
| `/getbtmode` | GET | Returns current NVS bt_mode string |
| `/getautotime` | GET | Returns `1` or `0` |
| `/getlasttext` | GET | Returns last saved text |
| `/getwelcometext` | GET | Returns saved welcome text |
| `/api/live` | GET | ELM327 diagnostic snapshot JSON |
| `/affa3/setMenu` | GET | Send menu frame (`?caption=`, `?name1=`, `?name2=`, `?scrollLock=`) |
| `/affa3test` | GET | Affa3 display test page |
| `/emulate` | GET | Button emulation page |
| `/emulate/key` | POST | Inject a key press (`?key=`, `?hold=`) |
| `/update` | GET | ElegantOTA firmware upload |

---

## Serial commands (115200 baud)

| Command | Example | Effect |
|---|---|---|
| `e` | `e` | Enable display |
| `d` | `d` | Disable display |
| `st <HHMM>` | `st 1430` | Set display clock |
| `msr <text> [delay_ms]` | `msr HELLO 300` | Scroll text right |
| `msl <text> [delay_ms]` | `msl MEGANE 250` | Scroll text left |

---

## ELM327 / OBD diagnostics

> **Note:** ELM327 connection is currently disabled in firmware (`connectToElm()` is commented out). The infrastructure is complete but needs WiFi STA / BLE coexistence tuning before re-enabling.

WiFi STA connects to `V-LINK` (open network, TCP `192.168.0.10:35000`). Cycles through PID plans querying:

| CAN header | ECU |
|---|---|
| `7E0` | Engine |
| `743` | Gearbox / transmission |
| `744` | HVAC |
| `745` | Additional |
| `74D` | Gearbox / alternator |

Results exposed at `GET /api/live` as JSON. To add a PID plan: create `PidPlan_XXX.h` and register it in `buildCombinedPlan()` inside `MyELMManager.h`.

---

## Source layout

```
src/
├── main.cpp                    — setup(), loop(), HandleKey(), initDisplay()
├── apple_media_service.cpp/.h  — BLE AMS client (NimBLE)
├── bluetooth.cpp/.h            — BLE scan + connect + CTS
├── current_time_service.cpp/.h — BLE Current Time Service (clock sync)
├── secrets.h                   — WiFi credentials (not tracked)
│
├── display/
│   ├── IDisplay.h              — pure virtual display interface
│   ├── AffaDisplayBase.cpp/.h  — ISO-TP CAN engine, sync state machine
│   ├── AffaCommonConstants.h   — shared enums (AffaKey, AffaError, SyncStatus…)
│   │
│   ├── Affa2/
│   │   ├── Affa2Constants.h         — Affa2 CAN packet IDs + namespace
│   │   ├── Affa2Base.cpp/.h         — Affa2 protocol implementation (base class)
│   │   ├── Affa2Display.h/.cpp      — 8-segment variant, Spotify title scroll
│   │   └── Affa2MenuDisplay.h       — full-LED variant stub
│   │
│   └── Affa3Nav/
│       ├── Affa3NavConstants.h
│       ├── Affa3NavDisplay.cpp/.h   — full media screen + menu system
│       ├── AuxModeTracker.cpp/.h
│       └── Menu/
│           ├── Menu.cpp/.h          — hierarchical menu (Field, List, MultiField)
│
├── commands/
│   └── DisplayCommands.cpp/.h  — HTTP command dispatch layer
│
├── effects/
│   └── ScrollEffect.h          — blocking scroll helper (boot / serial)
│
├── server/
│   └── HttpServerManager.cpp/.h — PsychicHttp routes + HTML page
│
├── utils/
│   ├── AffaDebug.h
│   └── CanUtils.cpp/.h         — CAN frame send helpers
│
└── ElmManager/
    ├── MyELMManager.h/.cpp     — ELM327 TCP session + PID cycling
    ├── DiagPlanCommon.h        — MetricDef, PidPlan types
    └── PidPlan_*.h             — per-ECU PID definitions
```
