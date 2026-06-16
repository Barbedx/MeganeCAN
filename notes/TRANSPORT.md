# Display transport routing — one mode instead of four flags

## Goal
A clean, transport-independent way to choose **where outbound display frames go** and
**who closes the loop (ACKs)**, so the same firmware can drive a real CAN panel, a
virtual panel, or **both at once for testing** — and so new transports (UART, ESP-NOW)
are just another sink. Replaces the scattered flags (`skip_funcreg`, `_emuSelfAck`,
full-emu `enable`, `busAlive`) that made this hard to reason about.

## The model (already mostly present, just implicit)
Both send paths converge at `HwCanBus::send`:
- `affa3_do_send` → `_bus->send` (showMenu/setText/highlight/setTime …)
- `CanUtils::sendCan` → `HwCanBus::send` (sync / keepalive / registration / keys)

`HwCanBus::send` runs **TX taps then the busAlive gate then CAN0**. The taps already are
the sinks:
- **WireSink**  = the `@TX` serial/WS mirror tap (observation; ALWAYS on, never gated)
- **VirtualSink** = the EmuBridge FeedTap → the twin (`onCanRx`); the twin is
  **passive** (`setEmulate(false)` = decode only) or **active** (`true` = decode + ACK)
- **CanSink**   = `busAlive` gate + `CAN0.sendFrame`

## The one knob: `Route`
```
CAN_ONLY          CanSink on.  Virtual passive (or off). Real panel owns ACK.        (car)
VIRTUAL_ONLY      CanSink off. Virtual ACTIVE (twin ACKs). No CAN touched.           (bench/dev)
CAN_AND_VIRTUAL   CanSink on.  Virtual PASSIVE (decode, NO ACK). Real panel ACKs.    (live-observe)
```
WireSink (`@TX`) is always on in every mode — it is observation, orthogonal to routing.

Mapping to today's flags (which become internal implementation details):
- full-emu ON              → `VIRTUAL_ONLY`
- self-ACK ON (no twin)    → `VIRTUAL_ONLY` fallback (sender self-ACKs) — twin makes this redundant
- default (twin decodes)   → `CAN_AND_VIRTUAL` passive  (this is the headline test mode)
- normal car               → `CAN_ONLY`

## Why CAN_AND_VIRTUAL is the headline
Real panel + virtual panel show the **same screen** at the same time: CAN transmits so
the real panel renders + ACKs; the twin decodes the identical frames always-live (Phase B)
without ACKing, so it never interferes. `/api/screen` + the proxy mirror the real panel
exactly — perfect side-by-side testing.

## Build plan (each step: build + native + @TX byte-equality, protocol-adjacent)
1. `HwCanBus::setTxEnabled(bool)` (default true) — gate CAN0 in addition to busAlive, so
   VIRTUAL_ONLY can suppress CAN even on a live bus. Behavior-neutral when true.
2. `DisplayTransport` (owns `Route`; refs HwCanBus + EmuBridge/twin). `setRoute(r)`
   configures TxEnabled + twin passive/active. The ONLY public knob.
3. main.cpp: build the transport in initDisplay; route default = CAN_AND_VIRTUAL (twin
   decodes, CAN transmits on a live bus — identical to today's behavior).
4. HTTP `/api/route?mode=can|virtual|both`; keep `/api/emu` + `/api/fullemu` as thin
   aliases (compat) that map onto routes.
5. Verify per mode on the bench: `@TX` bytes identical; VIRTUAL_ONLY emits full
   sequences with no CAN; CAN_AND_VIRTUAL decodes while (on the car) the real panel
   renders identically.

## Guardrails
Protocol-adjacent (the send path). Builders stay byte-identical; `@TX` frozen; busAlive
gate and the 2 s ACK wait unchanged. The transport only *selects sinks* — it never
rewrites a frame. Verify bytes + on-panel, not just a green build.
