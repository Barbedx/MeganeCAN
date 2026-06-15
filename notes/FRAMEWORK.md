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
2. 🟡 `IDisplay::recv(Frame)` — remove the CAN_FRAME leak (this PR, step 1).
3. Extract media (AMS) + notifications (ANCS) behind `IMediaSource`/`INotifSource`
   ports so the screen logic stops depending on the BLE stack directly.
4. `IConfigStore` port over NVS; the typed `Config` becomes the single source of truth.
5. `App` composition root + `IModule` lifecycle; main.cpp shrinks to `App app; app.run()`.
6. Small static `EventBus` for cross-feature signals (BT state, AUX, screen-changed).
7. Then: rewrite display `recv()` on `Frame` → host-test the AFFA3 handshake end-to-end.

Sources: hexagonal/ports-&-adapters (Cockburn; Wikipedia), C++ HAL via abstract
interfaces (Beningo, embeddedrelated.com; mbedded.ninja), ESP-IDF component model
(Espressif), event-driven embedded trade-offs (embeddedrt.com).
