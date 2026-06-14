# MeganeCAN — session handoff (read this first)

Clean-context dump so a fresh session can resume + **test immediately**. Architecture detail now
lives in `CLAUDE.md` (rewritten this session) and in Claude's memory; this file is the operational
"how to pick up and test" guide + the running task list.

## TL;DR — where we are
ESP32 firmware that (a) talks to an iPhone over **BLE peripheral** (AMS/ANCS/CTS) and (b) drives a
Renault dashboard display over **CAN** by emulating the head-unit radio (AFFA3 protocol). This
session fixed the BLE pairing, killed CAN/memory crashes, made the WiFi dashboard stable, started a
**PC-side CAN display emulator**, and validated + documented the radio/display architecture.

Work is on branch **`feature/ble-can-bench-fixes`** (commits below). **Push is blocked** (403 —
needs the `Barbedx` GitHub account; `andriipetruk-hue` can't push). Commits are local only; ff-merge
to `main` when ready.

## Hardware & ports (this bench)
- **Bench board: classic ESP32 WROOM on `COM5`** (Silicon Labs CP210x, auto-reset works). Env =
  **`esp32dev`**. Flash ~91%. Public BLE MAC `f4:65:0b:58:88:d8`. **No CAN transceiver** on the bench.
- The **car** has an **ESP32-C3 SuperMini** (env `esp32dev-mini`, native USB). That is the real target.
- PlatformIO CLI: `~/.platformio/penv/Scripts/pio.exe`. **Always pass `-d <repo path>`** (shell cwd drifts).
  Repo: `c:\Users\Andru\source\repos\MeganeCAN`.

## ► How to start testing (the loop)
1. **Serial proxy (shared live log) — run in background:**
   ```bash
   cd /c/Users/Andru/source/repos/MeganeCAN/tools && \
   ~/.platformio/penv/Scripts/python.exe serial_proxy.py COM5 115200 8080
   ```
   - Browser: **http://localhost:8080** (live serial over SSE; user + Claude watch the same stream;
     input box sends commands back to the board).
   - Claude reads/greps the file **`tools/serial.log`** (truncated on each proxy start).
2. **Build:** `~/.platformio/penv/Scripts/pio.exe run -e esp32dev -d /c/Users/Andru/source/repos/MeganeCAN`
3. **Flash (needs COM5 free → stop the proxy first, then restart it after):**
   `... run -e esp32dev -t upload --upload-port COM5 -d <repo>`
   - Pattern every flash: **TaskStop the proxy → upload → restart the proxy → grep serial.log**.
4. **If BLE bonds won't persist** (`bonds stored: 0` after a real bond): the board NVS is corrupt →
   `... run -e esp32dev -t erase -d <repo>` then upload. (This was THE root BLE bug this session.)

### Web dashboard / device state right now
- Device is on **WiFi STA `192.168.100.87`** (home network `Yozhik`), mDNS `meganecan.local`
  (STA only; from Windows use the IP — `.local` needs Bonjour). AP fallback = `ESP32_MeganeCan_AP` /
  `Megane2004` → `http://192.168.4.1`.
- **`skip_funcreg` is currently TRUE** (set via dashboard this session) so the display sends text
  frames on the bench without a real display ACKing the sync. To capture menu/media CAN frames:
  open the menu via the dashboard or `curl -X POST http://192.168.100.87/emulate/key --data
  "key=321&hold=0"` (key is **decimal**: 0x141 = 321 = encoder RollDown).

### iPhone pairing (first time on a fresh/erased board)
iOS Settings → Bluetooth does NOT list this peripheral for first pairing (Apple quirk). Use a BLE
app (BLE Hero / LightBlue) ONCE to connect to **`MCD1`** + accept the pairing prompt; after the
first persisted bond iOS auto-reconnects silently with no app. Forget stale `MCD1`/`CTRL 01` on the
phone + toggle BT if it misbehaves. Success markers in log: `Auth complete: bonded=1 ... bonds
stored=1` then `AMS started`.

## What this session DID (commits on feature/ble-can-bench-fixes)
- `57cf22c` BLE advertising in the primary packet (name `MCD1`) + bench-safe CAN TX gate
  (`CanUtils::busAlive()` — no transceiver → suppress TX, avoids the TWAI `twai.c:184` reboot loop)
  + the `tools/serial_proxy.py` tool + BLE disconnect-reason decoding & bond logging.
- `1762589` WiFi+BLE coexistence: relax BLE conn params on connect (`updateConnParams` 30–50ms +
  slave latency) so the dashboard isn't starved; bump PsychicHttp `max_uri_handlers` 48→64.
- `7814ac5` Memory/stability: **removed the 16KB RAM ring log** (use the serial proxy now),
  HTTP `lru_purge_enable` + `max_open_sockets=4` (fix dashboard wedge), dashboard no longer
  auto-scans WiFi on load + slower polling, scan gated on largest *contiguous* block, throttled
  serial spam, loop `[heap]` watchdog.
- `9dd08cf` Emit outbound CAN frames as `@TX <id> <bytes>` (CanUtils, before the TX gate) — the
  feed for the PC display emulator.
- `4293cab` Rewrote `CLAUDE.md` architecture to the validated radio/display model.
- `538cf97` `AppConfig` — cache NVS config in RAM at boot (getters read RAM, not NVS) → stops the
  `nvs_open failed: NOT_FOUND` spam + per-request churn. (Task **A1**, done + verified.)
- `ef95650` consolidate 5 dashboard pollers into one `/api/dashboard` (Task **A2**, done + verified).

Heap after all this: ~62KB free / ~45KB largest contiguous block with BLE connected, stable under
dashboard hammering (was wedging at ~24KB/14KB before).

## Task list — DONE vs NEXT
**A — ideal memory architecture (agent-ranked):**
- A1 ✅ cache NVS config in RAM (`AppConfig`) — commit `538cf97`.
- A2 ✅ consolidate the 5 dashboard pollers into ONE `/api/dashboard` (commit `ef95650`). JS now
  splits fetch/render: `renderMedia/Notifs/Bt/Wifi/CanSeen(d)` + one `refreshDashboard()` poller.
  `/api/dashboard` returns `{media,notifs,bt,wifi,can}` (verified well-formed). Browser-render not
  yet eyeballed by the user — confirm the cards still populate.
- A3 ⏳ NEXT — stream JSON instead of building `String`; A4 serve dashboard HTML from LittleFS (1MB
  spiffs partition); A5 fixed `char[]` over `String` churn. Also: `/getlasttext`/`/getwelcometext`
  still open the `"display"` NVS namespace (2 residual NOT_FOUND) — fold into a cache too.

**B — CAN display emulator (the big goal, foundation started):**
- B1 `WireProto.h` — one documented UART contract (user's idea): `@TX`/`@RX`/`@EV` (fw→PC),
  `@KEY`/`@INJ` (PC→fw). v1 compact tags, v2 optional JSON. Header + the proxy parser = single truth.
- B2 PC-side AFFA3 decoder + virtual display in the proxy page: reassemble ISO-TP (single `[len]`,
  multi `0x20+N`), decode caption (offset 0x1A) + rows (`0xD` separators), render the screen.
- B3 closed-loop ACK: PC injects `@INJ (id|0x400) 74…` (DONE) / `30 01 00…` (PARTIAL) so
  `affa3_do_send` skips its 2s no-display timeout → bench runs full speed = real test rig.
- **Goal:** reverse-engineer the **AFFA3NAV navigation screen** so iPhone ANCS/Apple-Maps drive
  turn-by-turn arrows. User will record real CAN logs **in the car** next.

**C — docs:** `README.md` still says BLE **central** (stale — we're peripheral) + lacks the
radio/display matrix. Fix it. (`CLAUDE.md` already updated.)

## Architecture quick-reference (full detail in CLAUDE.md + Claude memory)
ESP sits between radio and display on CAN; emulates the radio. Two displays (monochrome large /
segment text-only), two radio protocols: **Carminat** (mono only) and **UpdateList** (both:
`UpdateListMenuDisplay`=mono, `UpdateListDisplay`=8-seg). `display_type` NVS picks the driver
(manual — auto-detect impossible while passive). Validated CAN IDs: Carminat sync `0x3AF`/reply
`0x3CF`, ctrl+text `0x151`, keys `0x1C1`, filler `0x00`. UpdateList sync `0x3DF`, ctrl `0x1B1`,
text `0x121`, keys `0x0A9`, filler `0x81`. ISO-TP send in `AffaDisplayBase::affa3_do_send`;
per-frame ACK on `(sentID | 0x400)`: `0x74`=DONE, `30 01 00`=PARTIAL. `skip_funcreg`: false=ESP is
the radio (registers), true=real radio present (ESP passive).

## Git state
- Branch `feature/ble-can-bench-fixes`. Tree clean at handoff (top commit `538cf97`).
- **Push blocked** (403, `andriipetruk-hue` lacks access to `Barbedx/MeganeCAN`). ff-merge to local
  `main` and push from an account with access when ready.
- Reference sandbox (proven BLE approach) lives at `../NimBLE-AMS-CNTRL-01`.

## Gotchas / invariants (don't regress)
- Flashing needs COM5 free → stop the proxy first.
- BLE callbacks never block / never GATT-write-with-response (deadlocks the host).
- Bench has no CAN bus → `busAlive()` gates TX; in the car, RX unlocks TX. Don't transmit unguarded
  (TWAI assert reboot loop).
- WiFi+BLE one radio: keep `setSleep(true)` AND the relaxed conn params, or the dashboard starves.
- Keep memory lean: no big RAM buffers / `String` churn in HTTP; the device runs near the heap edge.
