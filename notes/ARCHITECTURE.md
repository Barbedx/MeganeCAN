# MeganeCAN — architecture & memory map (read this to orient fast)

One-page model of the system, the memory budget, and the dev loop. For the deep CAN
protocol details see `CLAUDE.md`; for "how to pick up and test" see `HANDOFF.md`.

## The big picture
ESP32 sits on the Renault CAN bus **between the head-unit radio and the dashboard
display**, emulating the radio to drive the display (AFFA3 protocol), while also being
a **BLE peripheral** reading the iPhone's AMS/ANCS/CTS. Topology: `radio ⇄ ESP32 ⇄
display` (no other nodes). The display auto-detects which radio it's talking to at the
registration handshake, so `display_type` is a manual NVS setting.

## Layered architecture (the refactor backbone)
Everything below L0 speaks the portable `Frame` type (no `CAN_FRAME`/Arduino), so it
compiles + unit-tests on the host. The seams are the swap points.

```
L0  Hardware            CAN0 (esp32_can), NimBLE, WiFi, PsychicHttp        target-only
L1  Bus seam            ICanBus (HwCanBus / LoopbackCanBus / ReplayCanBus)  + IBusTap
                        IClock (ArduinoClock / FakeClock)
L2  Wire transport      WireLink (SerialWireLink / WsWireLink) + WireProto  @TX/@RX/@KEY
L3  AFFA3 logic         affa/ (Frame, IsoTp, ScreenDecode, ScreenModel)     PORTABLE
    Virtual displays    vdisplay/ (IVirtualDisplay + Carminat/UL-seg/UL-lcd) PORTABLE
L4  Emulation / replay  emulation/EmuBridge, record/ReplayCanBus            PORTABLE
L5  Radio-side displays display/ (AffaDisplayBase → Carminat/UpdateList*)   target-only*
    App wiring          main.cpp, server/, wifi_manager, bluetooth, ElmManager
```
`*` L5 radio-side display classes still bundle CAN + UI + business logic and are NOT
yet portable — the main remaining debt (see Roadmap).

### Seams — how to swap a piece
- **Bus**: bind logic to `ICanBus`. `HwCanBus` (real), `LoopbackCanBus` (host loop),
  `ReplayCanBus` (.canlog playback). Observers attach as `IBusTap` (serial @TX mirror,
  WS @RX recorder, EmuBridge feed).
- **Clock**: `IClock` — `ArduinoClock` on target, `FakeClock` (manual advance) in tests
  so the 2 s ACK wait is instant.
- **Transport**: `WireLink` — Serial + WebSocket fan out from one `WireProto::emitTx`.
- **Display twin**: `IVirtualDisplay` — decode-only (passive) or ACK/sync/key (full).

## Memory budget (the device is RAM-tight)
Runtime heap with BLE+WiFi+HTTP+AMS live: ~**56 KB free / ~39 KB largest block**, dips
to ~**23 KB** during BLE/WiFi startup. **Largest contiguous block (`maxblk`) is the
number that matters** — it gates allocations, not total free.

- **Observe**: `GET /api/health` → `{heap:{free,min,maxblk,total},uptime_ms,ws:{...}}`.
  Serial `[heap] free/min/maxblk` every 10 s; warns when `maxblk < 20 KB`.
- **Notable static buffers**: WS double buffer 2×2 KB (`WsWireLink`), dashboard HTML
  + `/wire` PROGMEM (flash, not RAM), AMS/ANCS history, ELM value cache.
- **Discipline**: function pointers not `std::function`; fixed `char[]`/streaming over
  `String` churn in HTTP; WS drops when no client; one `/api/dashboard` poll; gate WiFi
  scans on `getMaxAllocHeap`; config cached in RAM (`AppConfig`), setters `ESP.restart`.

## Dev loop
- **Native tests** (no hardware): `pio test -e native` — 19 cases across bus/vdisplay/
  replay/emubridge. Needs a host `g++` (portable MinGW/w64devkit on PATH; or run via the
  gcc Docker image). `scripts/ci.{sh,ps1}` = native tests + both target builds.
- **Build/flash**: `pio run -e esp32dev -t upload --upload-port COM5` (stop the serial
  proxy first — it holds COM5 — then restart it). `esp32dev` = bench WROOM, no
  transceiver; `esp32dev-mini` = car C3.
- **Observe**: serial proxy `tools/serial_proxy.py` (renders the screen from @TX) and
  the wireless **`/wire`** page (live frames + ID filter + steering + record→.canlog +
  full-emulation toggle + ESP self-decoded screen).

## Roadmap — architectural debt (status)
1. **Decompose the radio-side display monolith** (`CarminatDisplay.cpp` ~1137 lines):
   🟡 *foundation laid* — `affa3_do_send` now sends through the injectable `ICanBus`
   seam (`setBus`), so the radio can be driven on the host / a pure virtual loop.
   *Remaining:* separate the menu model / pages / ELM from CAN transport, route the
   remaining `CanUtils::sendCan` sync/highlight sends through `_bus`, decouple from
   `Serial`/`Preferences` so the send path is host-testable.
2. **Slim `main.cpp`**: ✅ serial console → `console/SerialConsole`; FULL-EMULATION →
   `emulation/EmuBridge`. (`wireCommand` WS handler intentionally kept in main.)
3. **Split `HttpServerManager.cpp`**: 🟡 dashboard HTML → `server/DashboardPage.h`
   (941→685 lines). *Remaining (low risk):* JSON builders → `JsonBuilders.h`, the
   `/diag` + `/affa3test` inline pages out, group routes into `setupXxxRoutes()`.
4. **Typed, versioned config + fleet provisioning**: ✅ `AppConfig` schema version +
   `provisioned` first-run flag (with legacy migration) + `DisplayKind`/`BtKind` enums;
   `POST /api/setup` one-shot provisioning; `/api/health` reports `cfg`.
