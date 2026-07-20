# Architecture V2 — Dual-board CAN gateway + DUDU7 head unit integration

*Status: **V2.2** — V2.1 role swap (2026-07-19: WROVER = GW brain, C3 = peripheral) plus
change requests CR-01..CR-10 accepted 2026-07-20. Migration phases M1–M4 in §9. P0/P1 code
(portable modules + LinkProto + tunnel) was built exactly so this shuffle is a re-wiring of
mains, not a rewrite.*

## 0. Design principle (V2.2)

**GW (WROVER) is the only application processor. All other boards are replaceable
peripherals communicating through stable interfaces. Business logic must never depend on a
specific peripheral implementation.** Concretely:
- The peripheral is addressed only through LinkProto capabilities (§4) and `ICanBus` — never
  by name. Today it is an ESP32-C3 driving the AFFA3 display; tomorrow it may be an
  MCP2515/2518FD bridge or another MCU. (CR-02/03/04)
- The runtime protocol and the programming transport version independently (CR-01).
- Inside GW, modules talk through the EventBus, not each other (CR-07); only the Logger
  touches the filesystem (CR-08).
*Research inputs: aerodomigue/esp32-canbox-nissan (full RE), forum.dudu-auto.com d/1421
(mikescotland, Clio III — closest prior art, working system), DUDU wiki, smartgauges/canbox,
x0r.fr/blog/39 (Clio III main-CAN decode), Racelogic Mégane II DB, this repo's codebase map.*

## 1. What changed / goal

The OEM radio has been replaced by a **DUDU7 Android head unit** (FYT / Unisoc UIS7870, "DUDU OS").
The project expands from "drive the OEM display over multimedia CAN" to a full vehicle gateway:

1. **Second CAN bus** — listen on the vehicle main CAN (OBD pins 6/14, 500k) and feed telemetry
   to the DUDU7 over its canbox UART (Raise protocol), replacing a commercial canbox.
2. **Media from the head unit** — read the now-playing metadata the DUDU7 emits on the canbox
   UART (mikescotland verified DUDU sends title+artist on track change) and render it on the OEM
   display, as an alternative source to Apple AMS.
3. **Key routing matrix** — steering-wheel keys (from multimedia CAN today, GPIO matrix in the
   future) routed to any of: AMS remote commands, BLE HID keyboard, DUDU7 canbox UART.
4. Keep everything that works: OEM display drivers, BLE (AMS/ANCS/CTS + HID), web UI, OTA,
   bench emulator.

Native DUDU canbox profiles for Clio3/Mégane2 are officially crippled (SWC + parking sensors
only). The proven workaround (mikescotland): emulate the **Raise "Toyota RAV4 2018–2020"**
profile — the richest in FYT firmware (status-bar door/light icons, speed, RPM, odometer,
steering angle for reverse-camera guidelines).

## 2. System overview

```
                 ┌──────────────────────── car ────────────────────────┐
 Vehicle CAN ────┤ OBD pins 6/14, 500k          Multimedia CAN, 500k   │
 (engine/body)   │      │                        (display, SWC keys)   │
                 │      │ SN65HVD230                   │ SN65HVD230    │
                 │      ▼                              ▼               │
                 │ ┌─────────────┐  LinkProto UART ┌─────────────┐     │
                 │ │  GW board   │◄───────────────►│ DISP board  │     │
                 │ │ ESP32-WROVER│    (crossed)    │ ESP32-C3    │     │
                 │ │ 8MB PSRAM   │                 │ SuperMini   │     │
                 │ │  THE BRAIN  │                 │ (display    │     │
                 │ └──┬───┬───┬──┘                 │  co-proc)   │     │
                 │    │   │   │ BLE                └──────┬──────┘     │
                 │    │   │   ▼                          USB-CDC       │
                 │    │   │  iPhone (AMS/ANCS/CTS)      (bench proxy + │
                 │    │   │  DUDU7 (HID keyboard)        native flash) │
                 │    │   │ WiFi: web UI / OTA (rare)                  │
                 │    │   ▼ canbox UART 38400 (Raise RAV4)             │
                 │    │ ┌─────────────┐                                │
                 │    └►│   DUDU7 HU  │ (USB 5V powers both boards)    │
                 │      └─────────────┘                                │
                 └─────────────────────────────────────────────────────┘
```

**GW board (gateway/brain) = ESP32-WROVER 8MB PSRAM** — owns everything that grows: vehicle
main CAN (**TWAI listen-only — physically cannot disturb the bus**), `VehicleState` decode,
the DUDU7 canbox UART (Raise emitter + HU→box RX capture), **BLE** (iPhone AMS/ANCS/CTS +
DUDU7 HID keyboard), **WiFi** (AP/STA, web UI, OTA — rarely used, maintenance only),
media + key routing (MediaRouter/KeyRouter), PSRAM/LittleFS capture logs. Every future
feature (nav-screen RE, DUDU metadata, dashboards) lands here, where RAM and flash are
abundant.

**IOC board (IO Controller — the replaceable peripheral; today an ESP32-C3 SuperMini,
called DISP in code where it drives the display)** — a **thin peripheral with zero business
logic** (CR-02): multimedia CAN + AFFA3 display emulation (Carminat/UpdateList drivers), SWC
key capture (0x0A9/0x1C1) → `KEY_EVT` to GW, AUX detect, MediaInfo rendering fed by
`MEDIA_TEXT` from GW, USB bridge, link endpoint. Nothing else — every decision (routing,
modes, media source, key mapping) is GW's. Keeps native USB-CDC: the bench serial proxy for
display RE **and** the wired maintenance port for both boards (§3.4). No BLE, no WiFi, no
web. The architecture treats it as *a* peripheral, not *the* C3: a future MCP2515/2518FD
bridge or different MCU slots into the same LinkProto caps + `ICanBus` contracts (CR-03).

Why this split (V2.1 — the reverse of the first draft):
- **One TWAI controller per chip** is the only hard constraint forcing two boards; each board
  owns one CAN bus. (A future single-WROVER variant needs an MCP2515 on SPI — §P5, seam ready.)
- The BLE/WiFi/HTTP stack already builds and runs on classic ESP32 — the `esp32dev` bench env
  IS this chip. Migration is a re-target with +8MB PSRAM, not a rewrite; the C3's chronic RAM
  tightness (~62KB free with everything live) simply disappears.
- Both DUDU7 interfaces (canbox UART + BLE HID) and both iPhone services (AMS/ANCS) live on
  one board — no cross-board round-trips for keys→HU or media→router.
- The display protocol work (AFFA3) is done and car-validated; freezing it into a small
  peripheral firmware is the safest place for it. The link feeding it (MEDIA_TEXT/KEY_EVT)
  is byte-identical to what P1 already ships.
- mikescotland's two-MCU split proved the pattern; ours just puts the brain on the big chip.

## 3. Hardware

### 3.1 Power

Constraint: the WROVER is a **bare module with no regulator**, so it must be fed clean 3.3V.
Feeding it from the SuperMini's onboard LDO is viable **only because VH is radio-less**: a
WROVER that never initializes WiFi/BT draws ~50–100mA (CPU + flash + PSRAM), and the C3's
LDO (ME6211-class, ~500mA) can carry that on top of the C3's own BLE+WiFi peaks (~300–350mA).
It is still thermally marginal on a SOT-23 LDO at 5V→3.3V — treat it as the interim plan.

**One power architecture, no interim variants (CR-06):**

```
12V (ACC-switched)  →  Buck 5V  →  3.3V regulator (≥600mA, WiFi TX peaks)
                                        → GW (WROVER)
                                        → IOC / peripheral
                                        → CAN transceivers
```

- ≥220µF bulk capacitance at the WROVER's 3V3/GND pins, short thick wires, common ground.
- WROVER bare-module strapping: EN→3V3 via 10k, GPIO0 floating/high for normal boot (tie a
  button/pad to GND for the initial pre-solder flash), GPIO12 low/floating (flash voltage
  strap). Owner flashes the module on a dedicated programming jig before soldering.
- ACC-switched 12V = the gateway lives and dies with the ignition; no sleep logic
  (canbox-nissan pattern). The DUDU7 USB port is NOT a power source in the final design.

### 3.2 Pin map

**GW — ESP32-WROVER.** GPIO16/17 are PSRAM-reserved on WROVER — never use them.
| Function | Pins | Notes |
|---|---|---|
| TWAI (vehicle CAN) | RX=GPIO21, TX=GPIO22 | + SN65HVD230; TX pin unused in listen-only but wired for future |
| UART0 (debug/flash) | USB CP210x / jig | console + the one pre-solder flash |
| UART1 → DUDU7 canbox | TX=GPIO25, RX=GPIO26 | 38400 8N1, 3.3V TTL direct (verified practice) |
| UART2 → DISP link | TX=GPIO18, RX=GPIO19 | LinkProto, 460800 8N1 |

**DISP — ESP32-C3 SuperMini.** USB-CDC is native USB, so both hardware UARTs are free.
| Function | Pins | Notes |
|---|---|---|
| TWAI (multimedia CAN) | RX=GPIO3, TX=GPIO4 | unchanged |
| UART1 → GW link | TX=GPIO21, RX=GPIO20 | the pins already labeled TX/RX on the SuperMini |
| USB-CDC | native | console, flashing, serial proxy, GW maintenance tunnel |

Link wiring: DISP TX21→GW GPIO19, DISP RX20→GW GPIO18, GND–GND.

### 3.2a PCB provisions (CR-09, CR-10)

- **USB independence (CR-09):** route the GW's UART0 (TX0/RX0/EN/IO0) to a footprint for an
  optional USB-UART — even while maintenance runs through the IOC's USB. The C3 can then be
  retired (single-board variant, §P5) or a different CAN module substituted with no main-PCB
  redesign.
- **Expansion header (CR-10):** one universal header on the main PCB: UART, SPI, I²C, 3.3V,
  5V, GND, Reset, spare GPIO. Target add-ons without touching the board: MCP2515/2518FD
  (second CAN), GPS, IMU, LTE, an extra MCU.

### 3.3 Vehicle CAN tap

OBD-II socket: pin 6 = CAN-H, pin 14 = CAN-L (main bus, 500k). Multimedia CAN is also on the
OBD socket (pins 12/13) per the Clio III thread — a possible cleaner tap for DISP than the
current splice, worth verifying on the Mégane II harness. GW's transceiver: SN65HVD230,
**no 120Ω termination** (we tap a terminated bus; the "R" solder-jumper on CJMCU-230 modules
must be open).

### 3.4 Flashing — the end-user story (owner-final)

One product, one versioned **release bundle** (`gw.bin` + `disp.bin` + manifest, built
together — the two halves are never mixed across versions). No legacy images, no fallback
firmwares: clean-slate, polished to done. The WROVER is jig-flashed exactly once, pre-solder;
after that the end user has exactly two paths:

1. **USB into the SuperMini** (the always-wired maintenance port): the C3 flashes over its
   native USB; the WROVER flashes through the same cable via the USB↔link tunnel
   (PC tool → DISP USB-CDC → LinkProto `OTA_*` → GW OTA partition; same frames as the web
   path — the tool extends `tools/serial_proxy.py`).
2. **WiFi OTA** (GW hosts the web UI): "Update system" takes the release bundle, GW buffers
   it **entirely in PSRAM first** (both images fit in 8MB with room to spare — nothing
   flashes until the whole bundle is received and checksummed), then: flash DISP over the
   link → DISP confirms boot → GW writes its own inactive slot → reboot. Order matters:
   self-flash comes LAST so the PSRAM buffer survives until the peer is done.

Dual app slots on each chip are the *mechanics* of ESP32 OTA (a running image cannot
overwrite itself; boot-failure rolls back to the slot that booted last) — not a legacy-image
scheme. Absolute worst case for GW = jig clip on UART0 pads (keep TX0/RX0/EN/IO0 accessible
when soldering).

## 4. Inter-board protocols — LinkProto (runtime) + ProgProto (programming)

Portable module `src/link/` compiled into both firmwares **and** the `native` test env.

**CR-01: two independent protocols over one byte transport.** The COBS+CRC16 framing and the
LinkPort pump are the shared *transport*. On top of it:
- **LinkProto (runtime)** — HELLO/PING, KEY_EVT, MEDIA_TEXT, SIG_BATCH, DISP_CMD, HU_STATUS,
  CFG, CAP_CTL, LOG, RAW_FRAME, TIME, FILE (log pull). Versioned by `LinkProto::VERSION`.
- **ProgProto (programming transport)** — firmware upload, recovery, flashing (`OTA_*`,
  type range 0x70–0x7F, `src/link/ProgProto.h`). Versioned independently
  (`ProgProto::VERSION`) and deliberately frozen: runtime can evolve without ever touching
  the thing that reflashes boards, and vice versa.

- **Framing:** COBS-encoded frames delimited by `0x00`; payload = `[ver:1][type:1][seq:1]
  [payload…][crc16-ccitt:2]`. Max frame 128B. CRC over ver..payload.
- **Versioned hello:** on boot and every reconnect, `HELLO{proto_ver, fw_ver, caps bitmask}`
  both ways. Unknown types are skipped (forward compatible).
- **Capability bitmap (CR-05), `LinkProto::Caps`:** `DISPLAY` 0x01, `CAN` 0x02, `BLE` 0x04,
  `LOGGER` 0x08, `OTA` 0x10, `KEYBOARD` 0x20, `MEDIA` 0x40, `WEB` 0x80 (32-bit field, rest
  reserved). GW discovers what the attached peripheral can do from HELLO — an IOC without a
  display or a pure MCP-bridge peripheral degrades features automatically, never by ifdef.
- **Heartbeat:** `PING`/`PONG` at 1Hz; peer considered down after 3s — both sides expose link
  state. Link-down fallbacks: GW keeps canbox/BLE duty (its normal mode); DISP keeps the last
  screen + its own status text.
- **Message types (v1.1 — same ids as v1; V2.1 flips who sends what):**
  | Type | Dir | Content |
  |---|---|---|
  | `HELLO`, `PING/PONG` | both | as above |
  | `SIG_BATCH` | GW→DISP | decoded vehicle signals N × `{sig_id:1, value:i32}` — lets the OEM display show vehicle data later; optional, off by default |
  | `MEDIA_TEXT` | GW→DISP | `{field:1 (title/artist/album/source/state), utf8 text}` — the routed now-playing (AMS **or** DUDU metadata; DISP renders whatever GW's MediaRouter picked) |
  | `HU_STATUS` | GW→DISP | HU volume/source/freq — informational, for future display screens |
  | `KEY_EVT` | DISP→GW | `{AffaKey code, edge}` SWC keys; GW's KeyRouter fans out to AMS / HID / canbox `0x20` (keys pre-empt telemetry on the canbox UART — mikescotland rule) |
  | `DISP_CMD` (0x21) | GW→DISP | display steering for the web UI + tests: `{op:1, args…}` mapping the IDisplay surface (setText / showMenu / popups / key inject / enable) — keeps `/emulate/*` and the bench flows working with the web on GW |
  | `RAW_FRAME` | both | `{bus:1, id:2, dlc:1, data}` — bus 0 = multimedia (DISP→GW live view for `/wire`), bus 1 = vehicle, bus 2 = HU UART; injection in either direction for RE |
  | `TIME` | GW→DISP | clock sync (GW has CTS/NTP) |
  | `LOG` | DISP→GW | text log line (throttled) into GW's web log / serial |
  | `CFG_GET/SET/ACK` | GW→DISP | DISP NVS keys (`display_type`, `skip_funcreg`, …) — settable from GW's web UI without reflashing |
  | `CAP_CTL` | GW→DISP | multimedia-CAN capture control (DISP streams matching frames back as RAW_FRAME; bulk vehicle-CAN capture is GW-local) |
  | `FILE_LS/REQ/DATA/ACK` | both | chunked file pull (GW's LittleFS canlogs today; generic) |
  | `OTA_BEGIN/DATA/END/STAT` | **both** | firmware update over the link: GW web UI → DISP image; **and** PC → DISP USB tunnel → GW image (§3.4). CRC'd chunks, per-chunk ack, resume offset. ~1.2MB at 460800 ≈ 30–60s (bump to 921600 if wanted) |
- **No blocking anywhere:** both ends are byte-pump state machines in `loop()`; TX through a
  ring buffer; a full buffer drops lowest-priority messages (LOG first, then RAW_FRAME).

## 5. GW firmware (WROVER — the brain)

*Built in P1 as `[env:vehicle-wrover]` (vehicle-side modules only); migration M1 grows it into
`[env:gw-wrover]` by absorbing the C3's BLE (BleHub/AMS/ANCS/HID), WiFiManager, HttpServerManager
+ web pages, MediaRouter/KeyRouter and CTS time — the same code the `esp32dev` bench env already
compiles for this exact chip, now with PSRAM headroom. Display output leaves over the link
(`MEDIA_TEXT`/`DISP_CMD`); SWC keys arrive over it (`KEY_EVT`).*

**GW internal architecture (CR-04/07/08):**
- **`ICanBus` = the CR-04 `ICanInterface`** (kept under its existing name — same contract:
  `send/onReceive/isLive/poll` over portable `Frame`). Implementations: `HwCanBus`
  (ESP32 TWAI via esp32_can — the C3), `TwaiCanBus` (IDF TWAI listen-only — the GW),
  `LoopbackCanBus`/`ReplayCanBus` (native tests), future `Mcp2515CanBus`/`Mcp2518CanBus`
  (expansion header). ALL code touches CAN only through this interface.
- **EventBus (CR-07, `src/core/EventBus`)**: `CAN → Decoder → EventBus → {CarState, Display
  feed, BLE, Raise emitter, Logger, Web}` — modules subscribe to signal-change events instead
  of holding references to each other. The decoder publishes; consumers never poll each other.
- **Logging pipeline (CR-08)**: `Event → PSRAM ring → Logger → LittleFS`. FsLogger is the
  ONLY module that opens files; everything else emits events into the ring.

Vehicle-side modules (all portable except drivers):
- `vh/target/vh_main.cpp` — thin wiring; M1 renames/expands into the GW main.
- **`VehicleDecoder`** — data-driven table (constexpr array in `VehicleDbc.h`, canbox-nissan
  `FrameConfig/FieldConfig` pattern: id → byte extract → formula → named signal slot). Feeds
  `VehicleState` (all signals as scaled ints + timestamps). Indicators use the timestamp-latch
  debounce (500ms re-arm). Unknown-ID census kept for RE.
- **`CanboxEmitter`** — Raise framing `0x2E [cmd][len][payload][cs]`, cs = sum XOR 0xFF,
  38400 8N1, fire-and-forget + optional ACK consumption (DUDU replies 0xFF/0xF0; don't block
  on it). RAV4 message set with per-message schedulers (interval + on-change):
  | Cmd | Signal | Interval |
  |---|---|---|
  | `0x20` | SWC key (keycode, 0=rel/1=press/2=long) | on event, priority |
  | `0x24` | doors bitmask | 250ms + on-change |
  | `0x28` | outside temp, byte[5]=(t+40)*2 | 5s |
  | `0x29` | steering angle int16 LE ±540 | 200ms |
  | `0x7D 01` | lights bitmask | 200ms + on-change |
  | `0x7D 03` | speed uint16 LE ×100 | 500ms |
  | `0x7D 04` | odometer 24-bit LE | 10s |
  | `0x7D 0A` | RPM uint16 LE | 333ms |
  Known DUDU quirks: skip seatbelt/parking-brake addresses (spurious gearbox icons, broken in
  DUDU Raise Toyota); long-press behavior changed in DUDU OS 3.6 beta — keep key mapping
  configurable.
- **`HuRxParser`** — parses HU→box frames: documented `0x81` start/stop, `0x90` info request,
  `0xA6` time, `0xC0` source, `0xC2` tuner freq, `0xC4` volume — and the **capture path**: every
  unrecognized frame is logged raw to FS (this is how we RE the DUDU media-metadata message).
  Once the format is known, it becomes a first-class parser emitting `MEDIA_TEXT`.
- **`CanCapture/FsLogger`** — PSRAM ring (minutes of full-bus traffic) → LittleFS rotating
  `.canlog` files (same format as `tools/*.canlog` so existing tooling replays them). Triggered
  captures: "log everything for 60s", "log HU UART", "log unknown IDs only".
- **`LinkTunnel`** — the maintenance plane, **in the very first image that goes into the car**
  (requirement: never pull a board to reflash). Serves CFG_*, CAP_CTL, FILE_*, OTA_* over
  LinkProto — generic enough that in V2.1 it runs on BOTH boards (DISP gets OTA'd from GW's
  web; GW gets OTA'd through DISP's USB tunnel, §3.4). If the link is down, GW runs canbox +
  BLE duty standalone on its NVS config.
- **`VhConfig`** (NVS) — from day one: canbox enable/profile options, key-forward enable,
  capture defaults, decode-table overrides (per-signal enable + id remap for sniff-phase
  corrections), link baud. Everything the sniff campaign might want to tweak is a config, not
  a rebuild.
- **Safety:** TWAI in `TWAI_MODE_LISTEN_ONLY` (no ACK, no error frames — physically incapable
  of disturbing the vehicle bus). TX mode is a compile-time opt-in for the future OBD-request
  feature (fuel level needs a diag request; deferred). esp_task_wdt 5s; CAN-silence + link-loss
  are states, not reboots (the HU may be on while the car is off).

Partitions (`partitions_gw.csv`, WROVER-E N16R8 — owner confirmed **16MB flash** + 8MB
PSRAM): nvs 20K / otadata / app0 3M / app1 3M / LittleFS ~10M. Ten megabytes of capture
space = hours of full-bus `.canlog`, and the 3MB app slots never need thinking about again.
Verify with `esptool flash_id` at the one bench flash.

## 6. C3 refactors (P0 — done) → DISP firmware (M2)

The P0 refactors below were completed on the C3 and are exactly what makes the V2.1 role swap
mechanical: media/keys/link/bus modules have no idea which chip they run on. In M2 the C3
slims down to the DISP firmware: display drivers + multimedia CAN + SWC capture (`KEY_EVT`
out) + MediaInfo-from-link + `DISP_CMD` server + USB serial proxy + LinkTunnel (OTA + the
GW USB tunnel). BLE/WiFi/web compile out; the full v2.0 image stays in the other OTA slot as
fallback.

### 6.1 Source-neutral media model (`src/media/`)
- `struct MediaInfo` — neutral: title/artist/album/source name, playback state, elapsed,
  duration, rate, volume, shuffle/repeat, `lastUpdateMs` (all current display needs covered).
- `IMediaSource` — `poll()`, `const MediaInfo& current()`, `bool active()`, change callback.
- Implementations: **`AmsMediaSource`** (wraps AppleMediaService; keeps the elapsed-time
  extrapolation that `CarminatNowPlaying::tick()` currently does by pulling AMS directly),
  **`HuLinkMediaSource`** (fed by `MEDIA_TEXT`/`HU_STATUS` from LinkProto).
- **`MediaRouter`** picks the active source: config `media_source = ams | hu | auto`
  (auto: HU wins while its link is up and sending, else AMS).
- Refactor: `AffaDisplayBase::setMediaInfo(const MediaInfo&)`; `CarminatNowPlaying` becomes
  push-only (no `AppleMediaService::GetMediaInformation()` pull); `buildMediaJson`,
  `SerialConsole`, UpdateList drivers re-pointed. AMS types no longer leak outside
  `AmsMediaSource`.

### 6.2 Key routing matrix (`src/keys/`)
- `IKeySource` → emits `(AffaKey, edge)`: **`CanStalkSource`** (today's 0x0A9/0x1C1 path via
  display recv), future **`GpioMatrixSource`** (direct button matrix when/if the OEM display
  and its CAN drop out).
- `IKeySink` → **`AmsSink`** (AMS remote commands), **`BleHidSink`** (HID consumer keys),
  **`UartCanboxSink`** (LinkProto `KEY_EVT` → VH → Raise `0x20`).
- **`KeyRouter`**: per-key-class routing from config bitmask `key_sinks` (multiple sinks may
  be active; default mirrors today's `bt_mode` behavior). `HandleKey()` in main.cpp shrinks to
  a call into the router.

### 6.3 Bus/source hygiene (`src/bus/`)
- `Frame` gains `uint8_t source` (MM_CAN / VH_CAN / VIRTUAL / INJECT). Set in the single
  conversion point (`HwCanBus::ingest`) — localized change.
- `gotFrame` dual-dispatch fixed: display subscription goes through `HwCanBus::onReceive`,
  no more direct `display->recv` shortcut. `MAX_TAPS` 4→6.
- LinkProto endpoint on MM: `src/link/LinkPort` owning UART1; VH `RAW_FRAME`s enter as tagged
  frames (available to CanLog/web), never into `display->recv`.

### 6.4 Config / UI
- New NVS keys (`config`): `media_source`, `key_sinks`, `link_enabled` (+ schema bump, defaults
  preserving current behavior exactly: media=ams, sinks=per bt_mode, link on).
- Dashboard additions: link status + VehicleState tile (speed/RPM/temp/doors), media-source
  indicator. New "Vehicle board" page (see LinkTunnel) proxies VH config/logs/OTA — MM is the
  single web front-end for both boards. Watch the PsychicHttp handler ceiling (~66/96 used) —
  prefer one multiplexed `/api/vh` route + the existing `/api/dashboard` JSON over many new
  routes.

## 7. Build & repo layout

Single repo, shared portable core, two firmware entry points (final):

```
src/            shared modules             src/link/   LinkProto codec + LinkPort (native-tested)
src/media/      MediaInfo, sources         src/keys/   KeyRouter, sinks
src/vh/         vehicle domain (decoder, canbox, HU parser) + vh/target/ GW-only glue
src/display/…   AFFA3 drivers (DISP)       src/gw/     (M1) GW main + web glue
src/disp/       (M2) DISP main
```

Envs, final set:
- `[env:gw-wrover]` — GW firmware (M1): vehicle modules + BLE + WiFi/web + routers + link.
- `[env:disp-c3]` — DISP firmware (M2): display + multimedia CAN + link + USB proxy.
- `[env:esp32dev-mini]` — legacy full C3 image, kept until M4 cutover proves out.
- `[env:esp32dev]` — bench board (WROOM): today's full firmware for display-RE work.
- `[env:native]` — host tests (link codec, decoder tables, canbox golden vectors, media/keys,
  DISP_CMD/MEDIA_TEXT round-trip).
- The P1-transitional `vehicle-wrover` image was deleted (owner: no legacy) — `gw-wrover`
  covers everything it did.

## 8. Verified data annex (research results)

Raw source dumps preserved in **`notes/research/`**: `posts_*.txt` = dudu-auto d/1421 thread
(mikescotland's FULL Arduino sketches: post 32 = Raise VW version, post 47 = final two-board
Toyota RAV4 version — engine-CAN board + SWC board), `d775_*.txt` = d/775 protocol thread
(0x7D lights RE, OD-OLO-02 canbox internals), `x0r39.txt` = full Clio III main-CAN decode.

### 8.0 Mégane II bus topology
Three CAN links, all bridged by the **UCH** (body gateway), workshop manual section 88B:
engine/powertrain CAN (injection, ABS, airbag, EPS; 120Ω at injection ECU + UCH), comfort/body
CAN (UCH, cluster, climate), multimedia CAN (radio, display, nav). **OBD-II socket: pins 6/14 =
main CAN 500k (Racelogic-confirmed for Mégane II 2002-2010); pins 12/13 = multimedia CAN**
(confirmed on Clio III / Renault VISIO era cars — verify on ours; could replace the
behind-the-radio splice). Racelogic confirms these signals exist on the OBD bus: brake, coolant
temp, doors FL/FR, RPM, fuel consumption, full beam, handbrake, headlights, ignition
acc/on/starter, speed, indicators L/R, side lights, steering angle, throttle, wheel speeds ×4,
wipers. Bus stays partially alive at key-off (0x354/0x5C5/0x5FD/0x60D/0x715 keep transmitting).
Fuel *level* is NOT broadcast — diag request only (puts ECU in diag mode; deferred).
Pre-≈2005 caveat: earliest Mégane II radios used an analogue SWC/display path — ours is CAN
(the AFFA3 firmware works), so not a concern.

### 8.1 Mégane II main-CAN candidate IDs — hypothesis list, verify by correlation sniff
From x0r.fr/blog/39 (Clio III, same era/platform conventions; frkgnn partially confirmed on
Mégane II Ph2). Checksummed frames: 8-bit sum of other bytes, one's-complemented.

| ID | Content (Clio III decode) |
|---|---|
| `0x0C2` | steering angle b0-1 (−0x8000, 0.1°/LSB), rotation speed b2-3 |
| `0x181` | RPM b0-1 (/8); accel pedal b3; b5: bit3 clutch, bit0 brake |
| `0x1F9` | RPM b2-3 (/8) |
| `0x215` | b1 bit6 reverse gear |
| `0x354` | speed b0-1 ×0.01 km/h; distance b2-3 ×0.1; b4 bit4 brake pedal |
| `0x551` | b0 engine temp −40; b1.. fuel used (12500 = 1L) |
| `0x5C5` | b0 bit3 handbrake (mikescotland's code says bit2==0 → verify); mileage nibbles |
| `0x5FD` | b0-2 odometer; b3-5 vehicle age minutes |
| `0x60D` | b0-2 status bits: 23 boot, 22 RR door, 21 RL, 20 FR, 19 FL, 18 position lights, 17 dipped, 14 right ind, 13 left ind, 11 main beam, 10 key-on, 9 key-acc, 8 front fog, 5 doors locked, 4 boot locked, 2 rear fog; b4 outside temp −40; b5 engine temp −40; b6 bit4 reverse; b7 bits0-1 right-stalk up/down |
| `0x645` | b1 instrument backlight 0x00–0xFE (0xFF=off) → illumination/dimming source; b3-4 speed |
| `0x651` | b0: bit1 pass-airbag off, bit2 key acc/run; b1 bit0 driver belt |

Secondary hypothesis tables (other Renault platforms — same conventions, IDs NOT guaranteed):
- **Kaptur/Duster (CAN-Hacker):** `0x186` RPM (b1, ~raw×32); `0x217` speed b4-5; `0x3F7` gear
  PRNDL (b1: 0x0F=P, 0x12=R, 0x20=D); `0x5DE` turn lamps (b1: 0x40 right, 0x20 left);
  odometer via UDS `0x743` → `03 22 02 07`, resp `0x763` 3-byte km.
- **Mégane 3 RS DBC (krsche, big-endian):** `0x29A`/`0x29C` wheel speeds ×0.005 km/h;
  `0x0C6` steering angle b0-1 ×0.1 −3276.7°; `0x1F6` brake bit19; `0x352` brake pressure b3;
  `0x186` throttle bit47 len10 ×0.1 (**collision: 0x186=RPM on Kaptur vs throttle on M3RS —
  IDs shift between platforms, correlation sniff is mandatory**); `0x4F8` handbrake.
- Clio III handbrake conflict: x0r says 0x5C5 b0 bit3=on; mikescotland's code tests bit2==0.
  Verify both on our car.

Racelogic confirms all target signals exist on the Mégane II OBD bus at 500k, so the sniff
will converge. VH TX stays off (listen-only) throughout.

### 8.2 SWC keys, multimedia bus (already ours)
UpdateList `0x0A9` / Carminat `0x1C1`; frame `03 89 <hi> <lo>`, hold = bit 0x80/0x40; codes:
0=LOAD, 1=SRC→, 2=SRC←, 3=VOL+, 4=VOL−, 5=PAUSE, 0x0101 SEEK+, 0x0141 SEEK−. (Repo's
`AffaKey` is ground truth.) Note: Clio III uses a different scheme (BIC module, ID `0x58F`,
`89 …`/`80 01` press/release) — do NOT confuse mikescotland's SWC captures with ours.

### 8.3 DUDU7 facts
- Platform: FYT family, Unisoc **UIS7870**, "DUDU OS" (near-stock Android, third-party apps
  run fine — precedent: hvdwolf/FytFunctionalityExtender).
- Factory Set password **108** → "Display CANBUS Info" = live canbox UART viewer (our RE tool);
  wiki has the official protocol-capture procedure (press failing key 5× at 1s, film bytes).
- Canbox picker (Settings → Vehicle → Vehicle canbus → Car Model Selection, brand→model):
  Hiworld, OD/Oudi, RZC/Raise, LZ/Luzheng, XP/Xinpu, BNR, Daojun, XBS. Renault-relevant
  profiles seen in the wild: Raise **G-RZ-Renault60** (single CAN), **G-RZ-Renault61/RZ-61**
  (dual CAN, needs per-car `canbox.upg` in the box), Hiworld LNF2.10/LN06.20 (Megane2/Clio3
  harness H1H2LN060A). Native Megane2/Clio3 support is officially crippled (SWC+radar only).
- Target profile = **Raise / Toyota Corolla/RAV4 2018–2020** family (mikescotland's proven
  trick — richest FYT profile: status-bar door/light icons, speed, RPM, odometer, guidelines).
- HU→box media metadata: **undocumented but empirically real on DUDU** (mikescotland showed
  title/artist on his LCD). Documented HU→box: 0x81/0x83/0x84/0x90/0xA0/0xA6/0xC0/0xC2/0xC4.
  Plan B if the DUDU7 build we have doesn't send text: DUDU-side Android app
  (NotificationListenerService → WiFi to MM). Plan C: AMS stays (phone-sourced media).
- 3.3V TTL direct to HU UART verified in practice (canbox-nissan wires C3 GPIOs straight in).

### 8.4 Raise protocol — full wire reference
**Framing (both directions):** `[0x2E][cmd][len][payload…][cs]`, `cs = (cmd+len+Σpayload) XOR
0xFF`. UART **38400 8N1, 3.3V TTL** direct GPIO↔HU (proven). Verified example: Vol+ =
`2E 20 02 01 01 DB`. Spec ACK: receiver replies `0xFF` ACK / `0xF0` NACK within 10ms, retry ≤3
— FYT/DUDU works fire-and-forget (canbox-nissan just drains RX); implement ACK consumption,
never block on it.

**Box→HU, RAV4 variant (as canbox-nissan ships; intervals in §5):**
| Cmd | Payload |
|---|---|
| `0x20` | SWC key `[code, state]`, state 0=rel/1=press/2=long. RAV4 codes: `0x01` VOL+, `0x02` VOL−, `0x07` SOURCE (long=mute), `0x08` VOICE, `0x16` OK, `0x85` NEXT, `0x86` PREV, `0x88` TEL |
| `0x21` | trip: `[avgSpd*10 BE:2][elapsed_s BE:2][range_km BE:2][0x02=km]` (7B, 5s) |
| `0x22`/`0x23` | inst/avg fuel cons: `[0x02=L/100km][val*10 BE:2]` (1s / 5s) |
| `0x24` | doors mask: driver 0x80, pass 0x40, RR 0x20, RL 0x10, boot 0x08 |
| `0x28` | outside temp: 12B zeros except `[5]=(t°C+40)*2` |
| `0x29` | steering int16 **LE**, −540..+540 (drives camera guidelines) |
| `0x7D 01` | lights mask: parking 0x80, low 0x40, high 0x20, left-ind 0x10, right-ind 0x08 (canbox-nissan) — mikescotland's set differs (0x08 right, 0x10 left, 0x40 head, 0xC0 high): **verify on DUDU7** |
| `0x7D 03` | speed: `[spd*100 LE:2][0x00][0x00]` |
| `0x7D 04` | odometer: `[odo LE:3][0xF2][0x08][trip1:3=0][trip2:3=0]` (magic 0xF2 0x08 cargo-culted from captures) |
| `0x7D 0A` | RPM uint16 LE — canbox-nissan sends rpm*4, mikescotland says rpm/2: **verify** |

**Box→HU, VW-Polo variant (fallback profile; English spec in canbox-nissan
`docs/protocols/raise-vw-polo/`):** `0x20` keys (0x01 VOL+, 0x02 VOL−, 0x03 up, 0x04 down,
0x05 TEL, 0x06 MUTE, 0x07 SRC, 0x08 MIC; state 0/1/2), `0x14` illumination 0x00–0xFF,
`0x24` basic info (bit0 reverse, bit1 handbrake, bit2 lights), `0x22`/`0x23` rear/front radar
4B L/LC/RC/R (0x00 off, 0x01 near…0x0A far), `0x26` steering int16 BE ±0x1FF, `0x16` speed
=(D1*256+D0)/16, `0x41` 13B vehicle info (RPM, speed×100, V×100, temp×10, odo, fuel) — the
frame that fills DuduOS "car info"; 2B variant = warning mask (FL 0x01, FR 0x02, RL 0x04,
RR 0x08, trunk 0x10, handbrake 0x20, washer 0x40, belt 0x80).

**HU→box (documented set — our RX parser's known list):** `0x81` start/stop (b0 1=connect),
`0x83` vehicle-set, `0x84` common-set, `0x90` info request (`2E 90 01 1E 50`), `0xA0` amp,
`0xA6` **time sync**, `0xC0` source select (`2E C0 02 01 01 3B`), `0xC2` tuner freq
(`2E C2 04 01 3C 28 00 D4` = 103.0MHz), `0xC4` volume (`2E C4 01 04 36`; spec: bit7 mute,
bits6-0 vol 0-127), `0xC1` play mode, `0xC3` media playback disc/folder/track/time.
**Track title/artist text: undocumented but empirically sent by DUDU (mikescotland post 50)**
— capture-first, everything unknown goes to FS log.
DUDU quirks: seatbelt broken in Raise-Toyota (always ON); the seatbelt/handbrake address threw
spurious gearbox icons — skip it; long-press handling changed in DUDU OS 3.6 beta.
Alt framings if profile changes: Hiworld `5A A5 [len][type][payload][cs=sum-1]`; RZC-PSA
header `0xFD` @19200; SimpleSoft `AA 55`.

### 8.5 Engineering patterns worth copying (canbox-nissan / mikescotland)
- Indicator debounce: CAN blink pulses re-arm a 500ms timestamp latch → steady UART state.
- Send-on-change-or-interval scheduler per message; doors/lights also on-change.
- Key forwarding pre-empts telemetry; whole frames only, never byte-interleaved.
- Watchdogs: esp_task_wdt 5s panic; CAN-silence 30s → reboot only when battery >11V says the
  bus *should* be alive; error-counter cap (100) → reboot. LED codes: rapid=CAN alive,
  1s=silent bus, solid=boot.
- Powered from HU USB → lifecycle = ACC, zero sleep logic.
- canbox-nissan's JSON `FrameConfig/FieldConfig` decode tables (extract → formula
  SCALE/MAP_RANGE/BITMASK → named slot) = the shape for our `VehicleDbc.h`.
- Host-native Unity tests for the decode core (`[env:native]` + mocks) — we already have the
  same pattern; keep new layers in it.

### 8.6 Reference implementations & links
- github.com/aerodomigue/esp32-canbox-nissan — ESP32-C3+SN65HVD230→DuduOS, Raise RAV4;
  `docs/protocols/raise-toyota-rav4/` (Chinese PDF) + `raise-vw-polo/` (English MD);
  USB-CDC OTA protocol; companion app "esp32-canbox-manager".
- github.com/smartgauges/canbox — STM32, cleanest Raise/Hiworld frame builders (`canbox.c`);
  XDA thread 4057759 = protocol bible (HU→box tables). Fork: erik-rosen/canbox-xc90-my2008.
- github.com/darkspr1te/Work …/MctCoreServices/src/com/mct/carmodels/ — leaked RZC Java
  (`RZC_ToyotaSeriesProtocol.java`, `RZC_VolkswagenSeriesProtocol.java`) — definitive RE ref.
- github.com/icarome/VwRaiseCanbox — clean Arduino Raise-VW emitter API.
- mikescotland protocol PDFs (Google Drive): SWC/keys 1iFL_Nui0X3n9P1Bsn-8NDJzWbIL6fmh6,
  RAV4 full 1iFQtRCPRPOZsFAjxb1eWVp3IcNeYncvQ, earlier partial 1dSJY13rKw9sS4xLhNyVk89jmh0nHpHtw,
  his schematic 1jZfsXzPao81jlkQmT2NBGCT_h3H_-lT7 (all /view URLs on drive.google.com).
- Forums: forum.dudu-auto.com/d/1421 (main thread), /d/775 (protocol thread), /d/455 (native
  Megane2 profile crippled), /d/3468 (Renault profiles); wiki.dudu-auto.com (password-108
  capture procedure). x0r.fr/blog/39 (Clio III decode). canhacker.com/examples/renault-kaptur-can-bus.
- github.com/krsche/renault-megane-3-rs-can-dbc (real DBC); dirksan28/Scenic2DashCanEmu
  (our-platform cluster, UDS 0x743 gauge writes); manu-t/autoradio-interface (Clio2 UpdateList
  confirms 0x121/0x3CF/0x3DF/0x1B1); Racelogic "Renault-Mégane II (2002-2010)" PDF;
  megane.com.pl/topic/47797 (UpdateList + 0x0A9 key table); hackaday.io/project/27439.

### 8.7 P0 implementation anchors (exact touch points in this repo)
- `src/apple_media_service.h:29` — `MediaInformation` struct (fields to mirror in neutral
  `MediaInfo`); `.h:76` NotificationCb; `GetMediaInformation()` at `.cpp:49`.
- `src/display/AffaDisplayBase.h:14-16,33-36` — AMS forward-decl + `virtual setMediaInfo(const
  AppleMediaService::MediaInformation&)` + `tickMedia()` → retype to neutral `MediaInfo`.
- `src/display/Carminat/CarminatNowPlaying.cpp:146-159` — **the pull**: `tick()` calls
  `AppleMediaService::GetMediaInformation()` directly + extrapolates elapsed
  (`elapsed += dtMs/1000 * rate` since `mLastPlaybackInfoMs`) → must become push-only; the
  extrapolation moves into the source/`MediaInfo` helper. Also `Bluetooth::IsConnected()` gate
  at `:118` and `GetStatusText()` — becomes `IMediaSource::active()`/`statusText()`.
- `src/display/Carminat/CarminatDisplay.cpp:554-560` — `setMediaInfo` = collaborator update +
  `eventQueue.push(MediaInfoUpdate)`; `tickMedia():577` → `_nowPlaying.tick()`.
- `src/display/UpdateList/UpdateListDisplay.cpp:5-37` — builds "Artist - Title" from info;
  push-only already, easy retype. `UpdateListMenuDisplay.h:7` inherits it.
- `src/server/HttpServerManager.cpp:53-87` — `pbState()` + `buildMediaJson()` read AMS +
  `Bluetooth::IsConnected()` directly → read `MediaRouter::current()`.
- `src/main.cpp:293-354` `HandleKey` (btMode string compare, AMS/Hid calls) → `KeyRouter`;
  `:356-369` `onDataUpdateCallback` + `g_mediaInfo` cache → `AmsMediaSource`; `:459`
  `display->tickMedia()` gated by `BleHub::amsActive()` — must tick for HU source too.
- `src/bus/Frame.h` — add `uint8_t source`; the ONE conversion point is
  `HwCanBus::ingest`; `gotFrame` (`main.cpp:175-192`) also converts + calls `display->recv`
  directly (dual path to fix). `HwCanBus.h` `MAX_TAPS=4` — all 4 consumed (SerialMirror,
  WsRecorder, EmuBridge + headroom) → bump to 6.
- `src/utils/AppConfig.{h,cpp}` — SCHEMA_VERSION=1→2; add `media_source` ("ams"|"hu"|"auto"),
  `key_sinks` (bitmask: 1=AMS, 2=HID, 4=HU-UART; default derived from bt_mode for migration),
  `link_enabled`. `Load()` at `.cpp:15`.
- HTTP: PsychicHttp `max_uri_handlers=96`, ~66 used (`HttpServerManager.cpp:114`); prefer
  extending `/api/dashboard` + one `/api/vh` route.
- `platformio.ini` `[env:native]` `build_src_filter` — add `src/media/`, `src/keys/`,
  `src/link/` to stay host-testable. Partitions: `partitions_ota.csv` (C3, 4MB: 2×1.375MB OTA
  + 1MB spiffs unused); WROVER needs its own CSV.
- Serial console media commands (`src/console/SerialConsole.cpp`, `pp/nx/pv`) call AMS
  directly — route through KeyRouter/MediaRouter to honor the active sink.
- BLE key sink today: `Hid::press(KEY_MEDIA_*)` (`src/ble/HidRole.h`), AMS transport helpers
  (`apple_media_service.h:93-106`).

## 9. Rollout phases (each independently shippable, car stays functional)

**P0 — refactors on MM (no new HW):** MediaInfo/IMediaSource + MediaRouter; KeyRouter;
Frame.source + dispatch cleanup; config schema bump. Regression: car display + AMS + HID all
behave exactly as today. Native tests for the new layers.
*Status 2026-07-19: IMPLEMENTED (src/media/, src/keys/, Frame.source + busRx dispatch,
AppConfig v2, native tests test_media/test_keys; all envs build, 44/44 native tests pass).
Awaiting on-car regression check before P1.*

**P1 — VH bring-up (bench):** WROVER env, LinkProto (loopback-tested), VehicleDecoder against
replayed `.canlog` fixtures, FsLogger, **full LinkTunnel: config + capture control + log pull
+ OTA-over-link, exercised end-to-end on the bench** (C3 ↔ WROVER over the real UART wires).
One-time bench flash of the bare module via a USB-UART adapter (EN/IO0 strapping) — after
this, all updates ride the link. Exit criterion: flash a new VH image from MM's web page.
*Status 2026-07-19: CODE COMPLETE (src/link + src/vh + [env:vehicle-wrover] + MM MmLinkService,
/api/vh routes, /vh page with capture/config/log-download/OTA; test_link 13 + test_vh 21 native
tests; all 4 envs build). REMAINING: wire the real boards (§3.2), one-time bench flash of the
WROVER, then exercise capture/log-pull/OTA end-to-end over the physical UART — the exit
criterion needs hardware on the desk.*

### V2.1 role-swap migration (the final architecture; each step keeps a working fallback)

**M1 — GW firmware (bench):** new `[env:gw-wrover]` = P1's VH modules **+** the C3 stack that
already compiles for classic ESP32 (BleHub/AMS/ANCS/HID, WiFiManager, HttpServerManager +
pages, MediaRouter/KeyRouter, CTS). Display becomes a `RemoteDisplay : IDisplay` that emits
`MEDIA_TEXT`/`DISP_CMD` over the link; `KEY_EVT` flows in and feeds KeyRouter. Exit: on the
bench, GW serves the web UI over WiFi, pairs the iPhone, and the C3 (still running the full
v2.0 image with its link service) renders GW-routed media on the virtual display.
*Status 2026-07-20: CODE COMPLETE — src/gw/ (gw_main, RemoteDisplay, GwLink+LinkUi,
CanboxKeySink), LinkUi role facade (link-time selection, no ifdefs), partitions_gw.csv
(2×1.44MB app + 1MB littlefs); image 85.7% RAM 26.1%. All 5 envs + 78/78 native green.
Bench exit criterion awaits the wired boards.*

**M2 — DISP firmware:** `[env:disp-c3]` — display drivers + multimedia CAN + SWC→`KEY_EVT` +
MediaInfo-from-link + `DISP_CMD` server + USB serial proxy + LinkTunnel (incl. the GW USB
tunnel, §3.4) + the python tunnel tool. No BLE/WiFi/web. Exit: bench pair GW+DISP does
media, keys, display steering from GW's web, the release-bundle update flow (§3.4) end to
end, and a GW flash through DISP's USB.
*Status 2026-07-20: CODE COMPLETE — src/disp/ (disp_main, DispLink with DISP_CMD server +
@PROG USB↔link bridge, DispConsole, AncsStub), tools/flash_gw.py. Image 357KB / RAM 10.6%
(was 1.24MB as the full brain). Known caveat: the Carminat diag menu page assumes an attached
ELM (none on DISP) — guard before M4. Bench exit criteria await the wired pair.*

**M3 — BLE re-validation:** iPhone AMS/ANCS/CTS + DUDU HID against GW's classic-ESP32 BLE
(4.2 dual-mode vs C3's BLE5 — NimBLE code identical, bench env proves it builds/runs; verify
bonds persist + advertising invariants on real phones). Exit: same behaviors as the C3 image.

**M4 — car cutover:** solder per §3.2/§3.1 (dedicated 3.3V reg), GW to OBD 6/14 + DUDU USB +
canbox UART, DISP keeps its splice. Clean cutover, no legacy images (owner decision): if
something misbehaves, fix forward — both boards stay reachable through the SuperMini USB
(§3.4) without pulling anything from the dash.

### Feature phases (run on the final GW/DISP topology)

**P2 — car sniff (no reflashing trips):** GW + SN65HVD230 on OBD 6/14, listen-only, capture
campaign driven from GW's own web UI (start capture, drive the checklist, pull logs at home
over WiFi); verify §8.1 IDs on the Mégane II (idle/rev, roll, full lock both ways, all doors,
all lights, key positions). Freeze `VehicleDbc.h` v1; per-signal corrections that fit the
override config need no rebuild at all.

**P3 — canbox live:** DUDU7 profile → Raise RAV4; telemetry visible in DUDU UI (doors, speed,
RPM, temp, guidelines). SWC keys end-to-end DISP→GW→HU with latency <50ms. Confirm volume
keys feel instant (priority forwarding).

**P4 — media capture & render:** log HU→box during track changes (password-108 viewer + GW
capture); decode the DUDU media message; `HuLinkMediaSource` (now GW-local) → `MEDIA_TEXT` →
OEM display shows HU-sourced now-playing. Fallback decision point: if no text on UART → build
the DUDU-side app (Plan B).

**P5 — future options (designed-for, not built):** single-board GW (MCP2515/TJA1050 SPI CAN
as second controller — `ICanBus` seam ready — retiring the C3 entirely); GPIO button matrix
(`GpioMatrixSource` slot ready); dropping the OEM display (KeyRouter + sinks unaffected).

## 10. Risks / open items

| Risk | Mitigation |
|---|---|
| GW power (BLE always + WiFi peaks on WROVER) | Dedicated ≥600mA 3.3V regulator from day one (§3.1, owner-approved); bulk caps; measure under WiFi+BLE load in M4 |
| BLE behavior differs on classic ESP32 vs C3 | Same NimBLE code already builds/ran as the `esp32dev` bench env; M3 re-validates bonds/advertising with real phones BEFORE the car cutover |
| GW bricked by bad OTA | Bundle fully buffered in PSRAM before any flash; dual-slot boot rollback (OTA mechanics, not legacy images); §3.4 USB tunnel through DISP; worst case = jig clip on UART0 pads |
| DISP bricked | Native USB always wired — reflash in seconds |
| Mixed firmware versions across the two boards | Release bundle only (gw+disp built together); HELLO carries fw_ver — mismatch shows loudly in the web UI |
| DUDU7 doesn't emit media text on UART | Plan B: HU-side Android app over WiFi; Plan C: AMS unchanged |
| Mégane II IDs differ from Clio III table | P2 correlation sniff before anything depends on them; listen-only means zero risk while sniffing |
| DUDU Raise-Toyota quirks (seatbelt, long-press) | Skip broken addresses; key mapping configurable |
| HU USB 5V budget for two boards | Measure under WiFi+BLE load in P3; buck fallback |
| Link UART noise in car | COBS+CRC16, seq numbers, heartbeat; keys are edge events re-sent on release — a lost frame can't stick a key |
| Display latency over the link | MEDIA_TEXT/DISP_CMD are tiny at 460800 (<1ms/frame); the AFFA3 panel itself is the slow leg (~2s ACK windows) |
| WROVER flash size | RESOLVED: modules are 16MB (N16R8); partitions_gw.csv uses it — confirm with esptool flash_id at the one bench flash |
