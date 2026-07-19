# Architecture V2 — Dual-board CAN gateway + DUDU7 head unit integration

*Status: DESIGN — approved direction, phased rollout below. Date: 2026-07-19.*
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
                 │ │  VH board   │◄───────────────►│  MM board   │     │
                 │ │ ESP32-WROVER│    (crossed)    │ ESP32-C3    │     │
                 │ │ 8MB PSRAM   │                 │ SuperMini   │     │
                 │ └──────┬──────┘                 └──────┬──────┘     │
                 │        │ canbox UART 38400             │ BLE        │
                 │        ▼ (Raise RAV4)                  ▼            │
                 │ ┌─────────────┐                  iPhone (AMS/ANCS/  │
                 │ │   DUDU7 HU  │                  CTS, HID keyboard) │
                 │ └─────────────┘                                     │
                 └─────────────────────────────────────────────────────┘
```

**MM board (multimedia) = ESP32-C3 SuperMini** — the current firmware, evolved. Stays on the
multimedia CAN: OEM display emulation, SWC key capture (0x0A9/0x1C1), AUX detect, BLE, WiFi
(AP/STA), web UI, OTA. It is the brain and the only UI.

**VH board (vehicle) = ESP32-WROVER 8MB PSRAM (bare module)** — new firmware, same repo.
Listens on the vehicle main CAN (**TWAI listen-only mode — physically cannot disturb the
bus**), decodes to a `VehicleState` signal model, owns the DUDU7 canbox UART (Raise emitter +
HU→box RX capture), logs raw CAN + HU traffic to LittleFS/PSRAM for RE. **Radio-less by
design: WiFi/BT are never initialized** — config, log download, captures, and firmware OTA all
tunnel over the inter-board link and surface in MM's web UI. This kills the WROVER's WiFi
power peaks (the module then draws ~50–100mA) and removes a whole WiFi stack from the image.

Why this split (and not the reverse):
- The C3 firmware is proven in the car with tight RAM; moving BLE+display to WROVER restarts
  a year of hard-won stability. Zero-regression principle: MM keeps its job.
- The VH job (CAN RX decode + UART TX + logging) is exactly what PSRAM + big flash + 3 UARTs
  are good at, and it needs no BLE.
- mikescotland shipped the same two-MCU split (engine-CAN board owns the HU UART; the SWC
  board forwards key frames through it with priority). It works in daily use.

## 3. Hardware

### 3.1 Power

Constraint: the WROVER is a **bare module with no regulator**, so it must be fed clean 3.3V.
Feeding it from the SuperMini's onboard LDO is viable **only because VH is radio-less**: a
WROVER that never initializes WiFi/BT draws ~50–100mA (CPU + flash + PSRAM), and the C3's
LDO (ME6211-class, ~500mA) can carry that on top of the C3's own BLE+WiFi peaks (~300–350mA).
It is still thermally marginal on a SOT-23 LDO at 5V→3.3V — treat it as the interim plan.

**Interim (now):** DUDU7 USB 5V → SuperMini 5V pin → SuperMini LDO 3.3V → WROVER 3V3.
- Hard rule: VH firmware must never call `esp_wifi_*`/BT init (enforced by not linking them).
- ≥220µF bulk capacitance at the WROVER's 3V3/GND pins, short thick wires, common ground.
- WROVER bare-module strapping: EN→3V3 via 10k, GPIO0 floating/high for normal boot (tie a
  button to GND for the one-time bench flash), GPIO12 low/floating (flash voltage strap).

**Target (later, cheap):** a dedicated 3.3V regulator module (HT7833 / AMS1117 board / mini
buck) off the same 5V rail, or a WROVER devboard. Nothing else in the design changes.
- 5V source stays the **DUDU7 USB port** (ACC-switched): the gateway lives and dies with the
  head unit, no sleep logic needed (canbox-nissan pattern).

### 3.2 Pin map

**VH — ESP32-WROVER.** GPIO16/17 are PSRAM-reserved on WROVER — never use them.
| Function | Pins | Notes |
|---|---|---|
| TWAI (vehicle CAN) | RX=GPIO21, TX=GPIO22 | + SN65HVD230; TX pin unused in listen-only but wired for future |
| UART0 (debug/flash) | USB CP210x | console + flashing |
| UART1 → DUDU7 canbox | TX=GPIO25, RX=GPIO26 | 38400 8N1, 3.3V TTL direct (verified practice) |
| UART2 → MM link | TX=GPIO18, RX=GPIO19 | LinkProto, 460800 8N1 |

**MM — ESP32-C3 SuperMini.** USB-CDC is native USB, so both hardware UARTs are free.
| Function | Pins | Notes |
|---|---|---|
| TWAI (multimedia CAN) | RX=GPIO3, TX=GPIO4 | unchanged |
| UART1 → VH link | TX=GPIO21, RX=GPIO20 | the pins already labeled TX/RX on the SuperMini |
| USB-CDC | native | console, flashing, serial proxy |

Link wiring: MM TX21→VH GPIO19, MM RX20→VH GPIO18, GND–GND.

### 3.3 Vehicle CAN tap

OBD-II socket: pin 6 = CAN-H, pin 14 = CAN-L (main bus, 500k). Multimedia CAN is also on the
OBD socket (pins 12/13) per the Clio III thread — a possible cleaner tap for MM than the
current splice, worth verifying on the Mégane II harness. VH's transceiver: SN65HVD230,
**no 120Ω termination** (we tap a terminated bus; the "R" solder-jumper on CJMCU-230 modules
must be open).

## 4. Inter-board protocol — LinkProto

Portable module `src/link/` compiled into both firmwares **and** the `native` test env.

- **Framing:** COBS-encoded frames delimited by `0x00`; payload = `[ver:1][type:1][seq:1]
  [payload…][crc16-ccitt:2]`. Max frame 128B. CRC over ver..payload.
- **Versioned hello:** on boot and every reconnect, `HELLO{proto_ver, fw_ver, caps bitmask}`
  both ways. Unknown types are skipped (forward compatible).
- **Heartbeat:** `PING`/`PONG` at 1Hz; peer considered down after 3s — both sides expose link
  state (MM shows it in the dashboard; VH falls back to autonomous canbox operation, which is
  its normal mode anyway).
- **Message types (v1):**
  | Type | Dir | Content |
  |---|---|---|
  | `HELLO`, `PING/PONG` | both | as above |
  | `SIG_BATCH` | VH→MM | decoded vehicle signals: N × `{sig_id:1, value:i32}` (scaled ints) |
  | `MEDIA_TEXT` | VH→MM | `{field:1 (title/artist/album/source/state), utf8 text}` from HU |
  | `HU_STATUS` | VH→MM | canbox link state, HU volume/source/freq (0xC0/0xC2/0xC4 parses) |
  | `KEY_EVT` | MM→VH | `{AffaKey code, edge: press/release/long}` → VH translates to Raise 0x20 and forwards with priority over telemetry (mikescotland rule: key frames pre-empt, never interleave) |
  | `RAW_FRAME` | both | `{bus:1, id:2, dlc:1, data}` — sniff/inject for RE; VH→MM streaming is rate-limited, bulk capture goes to VH's local FS instead |
  | `TIME` | MM→VH | clock sync (MM has CTS/NTP) |
  | `LOG` | VH→MM | text log line (throttled) for the MM serial proxy / web log |
  | `CFG_GET/SET/ACK` | MM→VH | key/value config (VH NVS): capture filters, canbox options, decode overrides — settable from MM's web UI without reflashing |
  | `CAP_CTL` | MM→VH | capture control: start/stop, mode (all / unknown-IDs / HU-UART), duration |
  | `FILE_LS/REQ/DATA/ACK` | both | chunked file pull from VH's LittleFS (canlogs) with per-chunk CRC+ack+retry |
  | `OTA_BEGIN/DATA/END/STAT` | MM→VH | **firmware update over the link**: MM's web UI accepts a VH image upload, streams it in CRC'd chunks (per-chunk ack, resume), VH writes its OTA partition and reboots. ~1.2MB at 460800 ≈ 30s (bump link to 921600 if wanted) |
- **No blocking anywhere:** both ends are byte-pump state machines in `loop()`; TX through a
  ring buffer; a full buffer drops lowest-priority messages (LOG first, then RAW_FRAME).

## 5. VH firmware (new, `[env:vehicle-wrover]`)

Modules (all portable except drivers):
- `vh/main.cpp` — thin wiring, mirrors MM's setup/loop discipline.
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
  (requirement: never pull the board to reflash). Serves CFG_*, CAP_CTL, FILE_*, OTA_* over
  LinkProto. VH has **no WiFi/BT at all** — MM's web UI is the only front-end: a "Vehicle
  board" page with VehicleState, capture start/stop, log browser/download, config editor, and
  a VH-firmware upload form. If MM is absent, VH runs canbox duty headless on its NVS config.
- **`VhConfig`** (NVS) — from day one: canbox enable/profile options, key-forward enable,
  capture defaults, decode-table overrides (per-signal enable + id remap for sniff-phase
  corrections), link baud. Everything the sniff campaign might want to tweak is a config, not
  a rebuild.
- **Safety:** TWAI in `TWAI_MODE_LISTEN_ONLY` (no ACK, no error frames — physically incapable
  of disturbing the vehicle bus). TX mode is a compile-time opt-in for the future OBD-request
  feature (fuel level needs a diag request; deferred). esp_task_wdt 5s; CAN-silence + link-loss
  are states, not reboots (the HU may be on while the car is off).

Partitions (WROVER 4MB): nvs 20K / otadata / app0 1.2M / app1 1.2M / LittleFS ~1.4M.
(If the module turns out to be 8/16MB flash, grow LittleFS.)

## 6. MM firmware refactors (existing code)

Ordered by dependency; each lands green on the car before the next.

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

Single repo, shared portable core, two firmware entry points:

```
src/            (MM firmware + shared)     src/link/       LinkProto codec (shared, native-tested)
src/media/      MediaInfo, sources         src/keys/       KeyRouter, sources/sinks
src_vh/         VH firmware entry + VH-only modules (VehicleDecoder, CanboxEmitter, HuRxParser, FsLogger)
```

- `[env:esp32dev-mini]` (MM, unchanged) · `[env:vehicle-wrover]` (`board_build` WROVER,
  `BOARD_HAS_PSRAM`, `build_src_filter = -<*> +<src_vh/> +<link/> +<bus/Frame*>…`)
  · `[env:esp32dev]` (bench) · `[env:native]` grows tests for LinkProto codec, VehicleDecoder
  tables, CanboxEmitter framing (golden byte vectors from the RE'd protocols).
- CI builds all four envs.

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

**P2 — car sniff (no reflashing trips):** VH + SN65HVD230 on OBD 6/14, listen-only, capture
campaign driven entirely from MM's web UI (start capture, drive the checklist, pull logs at
home over MM's WiFi); verify §8.1 IDs on the Mégane II (idle/rev, roll, full lock both ways,
all doors, all lights, key positions). Freeze `VehicleDbc.h` v1; per-signal corrections that
fit the override config need no rebuild at all.

**P3 — canbox live:** DUDU7 profile → Raise RAV4; telemetry visible in DUDU UI (doors, speed,
RPM, temp, guidelines). SWC keys end-to-end MM→VH→HU with latency <50ms. Confirm volume keys
feel instant (priority forwarding).

**P4 — media capture & render:** log HU→box during track changes (password-108 viewer +
VH capture); decode the DUDU media message; `HuLinkMediaSource` → OEM display shows
HU-sourced now-playing. Fallback decision point: if no text on UART → build the DUDU-side app
(Plan B).

**P5 — future options (designed-for, not built):** single-board variant (MCP2515/TJA1050 SPI
CAN as second controller — `ICanBus` seam ready); GPIO button matrix (`GpioMatrixSource` slot
ready); dropping the OEM display entirely (KeyRouter + sinks unaffected).

## 10. Risks / open items

| Risk | Mitigation |
|---|---|
| SuperMini-LDO → bare WROVER power | Acceptable only with VH radio-less (~50–100mA); bulk caps + short wires; upgrade path = $1 LDO/buck module, nothing else changes (§3.1) |
| VH bricked by bad OTA-over-link | Dual OTA slots + `esp_ota_mark_valid` only after LinkTunnel handshake succeeds post-boot; rollback on watchdog; worst case = one bench reflash |
| DUDU7 doesn't emit media text on UART | Plan B: HU-side Android app over WiFi; Plan C: AMS unchanged |
| Mégane II IDs differ from Clio III table | P2 correlation sniff before anything depends on them; listen-only means zero risk while sniffing |
| DUDU Raise-Toyota quirks (seatbelt, long-press) | Skip broken addresses; key mapping configurable |
| HU USB 5V budget for two boards | Measure under WiFi+BLE load in P3; buck fallback |
| Link UART noise in car | COBS+CRC16, seq numbers, heartbeat; keys are edge events re-sent on release — a lost frame can't stick a key |
| MM RAM headroom for new modules | New code is small (router/structs); heavy stuff (logs, capture) lives on VH's PSRAM |
| WROVER flash size unknown (4 vs 8/16MB) | Partition CSV per size; check at bring-up |
