# MeganeCAN as a framework — target design (organize once)

This is the **"ESP middle" framework**: an ESP32 that sits in a car and composes
features — BLE/AMS media, ANCS notifications, a WiFi web portal, a CAN display driver,
a display emulator, and dual-radio (Carminat / UpdateList) support — with more to come.
The car, the display, even some approaches may change; the middle stays. So we organize
around stable *ports*, not around today's hardware.

## Principles (grounded in embedded best practice)
1. **Hexagonal / Ports & Adapters.** The domain core (protocol + screen/media/notif
   logic) depends only on **ports** (pure interfaces). Hardware/IO live in **adapters**
   that implement those ports. Swap a display, a radio, a transport → swap an adapter,
   never the core. (Beningo; mbedded.ninja; hexagonal architecture.)
2. **HAL via pure-virtual interfaces + dependency injection.** No singletons reaching
   into globals; pass ports in. Use **function pointers** (not `std::function`) on hot
   paths — no heap, no RTTI on a RAM-tight device.
3. **Modular features, one composition root.** Each capability is a self-contained
   module with a uniform lifecycle; one place (`App`) wires ports→adapters→modules.
   (ESP-IDF component philosophy.)
4. **Decouple only where it pays.** Prefer direct interface calls. Add a **small static
   event bus** (fixed subscribers, no heap) only for genuine N×M fan-out (e.g. "BT
   connected" → display + portal + emulator). Don't route hot CAN frames through it.
5. **Testability is the architecture's proof.** Every port has a fake (`FakeClock`,
   `LoopbackCanBus`, `ReplayCanBus`). `pio test -e native` is the regression net; a
   port with no fake is a design smell.

## Layers
```
L0  Vendor          CAN0(esp32_can), NimBLE, WiFi, PsychicHttp, NVS         off-limits to core
L1  Ports           ICanBus, IClock, WireLink, IDisplay(Frame), IBusTap,
                    IVirtualDisplay, + planned: IMediaSource, INotifSource,
                    IConfigStore, IEventBus                                  pure interfaces
L2  Adapters        HwCanBus, ArduinoClock, Serial/WsWireLink, NimBLE
                    AMS/ANCS/CTS, WiFiManager+HttpServer, NvsConfigStore     implement L1
L3  Domain core     affa/ (Frame, IsoTp, ScreenDecode, ScreenModel),
                    vdisplay/, radio-side display LOGIC (once Frame-based),
                    media/notification models                               PORTABLE + tested
L4  Features        BleMedia, Notifications, WebPortal, DisplayDriver,
                    Emulator(EmuBridge), DualRadioAuth, Recorder/Replay     modules
L5  App root        composition: build adapters, inject into features        main / App
```
The rule: **an arrow may only point down or sideways within a layer, never up.** The
core never `#include`s an adapter or a vendor header.

## The keystone: kill the `CAN_FRAME` leak in the display port
`IDisplay::recv(CAN_FRAME*)` leaks the vendor type into the port, which is why the
radio-side display (`CarminatDisplay`, `UpdateList*`) can't be host-tested. Migrate the
port to `recv(const Frame&)`. Step 1 (low risk): change the signature, shim
`Frame→CAN_FRAME` at the top of each `recv()` body (bodies unchanged). Step 2 (later):
rewrite the bodies to work on `Frame` directly + route their replies through `_bus`,
which makes the whole AFFA3 handshake (sync/ACK/keys) native-testable.

## Module lifecycle (target)
```cpp
struct IModule { virtual void begin() = 0; virtual void service() = 0; virtual ~IModule(){} };
```
`App` owns a fixed array of `IModule*`, calls `begin()` once and `service()` each loop.
Features get their ports by constructor injection. Adding a feature = write a module +
register it in `App` — no edits scattered through `main`.

## Roadmap to the framework (incremental, each shippable)
1. ✅ Ports for bus/clock/transport/virtual-display; fakes; native tests.
2. ✅ Display port is Frame-only + the radio SEND is host-tested. `IDisplay` no longer
   pulls esp32_can/CanUtils/Arduino; `AffaDisplayBase::affa3_do_send` rewritten on
   `Frame` (no CAN_FRAME/CanUtils/Serial), clock+bus injected → it compiles + unit-tests
   on the host (test_radio_send: the 14-frame ISO-TP sequence). *Next:* rewrite the
   `recv()` bodies on Frame + route their replies through `_bus` → host-test the inbound
   sync/ACK handshake too (currently still shimmed Frame→CAN_FRAME at entry).
3. Extract media (AMS) + notifications (ANCS) behind `IMediaSource`/`INotifSource`
   ports so the screen logic stops depending on the BLE stack directly.
4. `IConfigStore` port over NVS; the typed `Config` becomes the single source of truth.
5. `App` composition root + `IModule` lifecycle; main.cpp shrinks to `App app; app.run()`.
6. Small static `EventBus` for cross-feature signals (BT state, AUX, screen-changed).
7. Then: rewrite display `recv()` on `Frame` → host-test the AFFA3 handshake end-to-end.

## Research-backed refinements (deep-research, cited)
A multi-source review (ESP-IDF + Zephyr docs, a peer-reviewed DI paper, ETL) confirmed
this design and sharpened five points:
1. **Ports = a layered HAL.** ESP-IDF's LL→HAL→Driver one-directional stack and Zephyr's
   per-driver *struct of function pointers* are the canonical embedded ports&adapters.
   Our `ICanBus`/`IClock`/`WireLink` are exactly this. Refinement: split **immutable
   config (const, lives in flash)** from **per-instance runtime data (RAM)** in adapters.
2. **DI = static composition, no container.** Compile-time DI frameworks (boost-ext.di)
   add real binary cost (+1.4–3.2 KB on Cortex-M4); hand-wired static composition +
   function pointers (what we do) is the cheapest and is preferred for MCUs.
3. **Composition root = declarative registration + deterministic init order.** ESP-IDF
   `ESP_SYSTEM_INIT_FN` priorities / Zephyr `PRE_KERNEL_1/2`,`POST_KERNEL` levels are the
   model for `App`/`IModule`. Caveat (Zephyr maintainers): **static priority schemes are
   fragile** under customization — keep ordering explicit and few-leveled, not sprawling.
4. **Event bus = typed, statically-sized observer, never heap.** ETL `etl::observer` /
   jl_signal bound subscriber counts at compile time. Our `EventBus` must be fixed-size
   static (no `std::function`, no heap). Budget hard: classic ESP32 static DRAM ≈160 KB,
   BT alone ≈64 KB — every subscriber slot is counted.
5. **Fleet config = schema + per-device values.** ESP-IDF `mass_mfg` (schema CSV + per-
   device CSV → per-device NVS images) is the factory pattern; our typed `Config` +
   `provisioned` flag + `/api/setup` is the runtime half. Host TDD with test doubles
   (Grenning) is the established way to test this hardware-coupled code off-target — which
   is what our fakes (`FakeClock`/`LoopbackCanBus`) + `pio test -e native` already do.

Sources: ESP-IDF hardware-abstraction / startup / memory-types guides (Espressif);
Zephyr driver model + devicetree API (zephyrproject.org); compile-time DI cost & static
injection (SBLP 2025, sol.sbc.org.br); ETL observer (etlcpp.com); release/version &
provisioning (interrupt.memfault.com). Earlier: hexagonal (Cockburn/Wikipedia), C++ HAL
abstract interfaces (Beningo; mbedded.ninja).
