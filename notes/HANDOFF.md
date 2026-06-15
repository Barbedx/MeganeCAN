# MeganeCAN — session handoff (read this first)

Clean-context dump so a fresh session can resume + **test immediately**. Architecture detail now
lives in `CLAUDE.md` (rewritten this session) and in Claude's memory; this file is the operational
"how to pick up and test" guide + the running task list.

## TL;DR — where we are
ESP32 firmware that (a) talks to an iPhone over **BLE peripheral** (AMS/ANCS/CTS) and (b) drives a
Renault dashboard display over **CAN** by emulating the head-unit radio (AFFA3 protocol). This
session fixed the BLE pairing, killed CAN/memory crashes, made the WiFi dashboard stable, started a
**PC-side CAN display emulator**, and validated + documented the radio/display architecture.

**All merged to `main`** (PR #4, merge `a1f109b`, 22 commits) and pushed to `Barbedx/MeganeCAN`.
Local `main` == `origin/main`; the `feature/ble-can-bench-fixes` branch is deleted. The git remote
is now **just `origin` = `Barbedx/MeganeCAN`** (the old `andriipetruk-hue` fork remote was removed).
Resume work directly on `main` (or a fresh branch off it).

## ▶ IN THE CAR NEXT (2026-06-15 — live capture session)
Heading to the car with the laptop to watch **real-time** what the firmware sends. Goal: eyeball the
newly reimplemented **`showConfirmBoxWithOffsets`** popup on the real monochrome display, and keep
recording AFFA3NAV frames toward turn-by-turn arrows.
- **Just reimplemented** (commit `737c9f9`): `CarminatDisplay::showConfirmBoxWithOffsets(caption,
  row1,row2)` — was an empty stub, the only working version had been a raw `CanUtils::sendCan` free
  function. Now routed through `affa3_send` (busAlive gate + per-frame ACK + `@TX` mirror + self-ACK),
  reproducing the original wire bytes exactly: first frame `10 6F | 21 05 00 00 01 49`, then 15
  consecutive frames; caption@0x1A (max 7 chars), row1/row2@0x20 with `0x0D` separator, bounded <0x36.
  Text is transliterated like `showMenu`. The old raw free function was removed.
- **It is NOT wired to any route/key yet** — `DisplayCommands::setTextBig` still `throw`s
  ([DisplayCommands.cpp:65](../src/commands/DisplayCommands.cpp)); the `_display.showConfirmBoxWithOffsets(...)`
  call is commented out one line below. To trigger it in the car either: (a) un-stub `setTextBig` and
  add a quick HTTP route, or (b) call it from a temporary debug hook. Decide on the bench/car.
- **On the car** self-ACK is NOT needed (the real display ACKs); on the bench it IS (`/api/emu?on=1`)
  or only frame 0 emits. Watch the live `@TX` stream in the proxy either way.
- Compare the real display rendering vs. what the proxy's virtual Carminat decodes — confirm the
  caption/rows land where expected (the offsets above are reverse-engineered, may need nudging).

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
- `a10d54d` README: BLE peripheral + radio/display matrix + display_type values (Task **C**).
- `ae47a80` `WireProto.h` UART contract; CanUtils emits `@TX` through it (Task **B1**, verified).
- `e6f4b92` bench emulator self-ACK + `@INJ`/`@EMU` → emit full AFFA3 sequence (Task **B3a**).
- `3ff0100` PC-side AFFA3 virtual Carminat screen in the proxy (Task **B2**, verified live).

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

**B — CAN display emulator — WORKING (decodes real AFFA3 frames):**
- B1 ✅ `WireProto.h` UART contract (commit `ae47a80`).
- B3a ✅ bench emulator **self-ACK** (commit `e6f4b92`): with no real display, `affa3_do_send`
  self-acknowledges each frame so the COMPLETE multi-frame AFFA3 sequence is emitted as `@TX`.
  Enable: **`GET /api/emu?on=1`** (reliable). Serial `@EMU 1` / `@INJ <id> <bytes>` also exist but
  serial INPUT from the proxy isn't reaching SerialCommands yet (see Gotchas) — use HTTP for now.
- B2 ✅ PC-side AFFA3 decoder + **virtual Carminat screen** in the proxy page (commit `3ff0100`).
  Decodes the real `@TX` stream: ISO-TP reassembly (frame0 0x10 = 8 bytes; consecutive 0x2N append
  bytes[1..7]); menu payload (96B): scrollLock[10], header[11..36], item1 marker[38]+text[39..63],
  item2 marker[65]+text[66..95]; highlight = single `07 29 01 <rowId>` frame (7E=item1, 7F=item2).
  **Verified live:** header "Main Menu", items "Voltage: 0V" / "Boost: 0mbar", selection highlighted.
  To drive on the bench: open the menu with **Load-hold** (`/emulate/key key=0 hold=1`), navigate
  with RollDown (`key=321`) / RollUp (`key=257`); `skip_funcreg=TRUE` + `/api/emu?on=1` must be on.
- B2 ✅✅ verified live for **all three screens** (commits `3ff0100`, `…`): **menu** (Main Menu /
  Voltage / Boost + highlight), **now-playing** (Spotify: `> Spotify [hh:mm]` / artist-title /
  `XXX___ 0:49/2:26` progress bar), **notifications** (Telegram/Gmail popup, 6s then back to media).
  All decoded from real frames. Drive media: `/api/emu?on=1` + `/setaux` (AuxModeTracker auto-AUX in
  the car when the radio selects AUX) + music playing + menu closed; notifications interrupt for 6s.
- B2 ✅ **transliteration** fixed (commit … `Transliterate all text in showMenu`): Cyrillic media
  titles / app names no longer leak UTF-8 → mojibake; showMenu is the single ASCII choke point.
- B2-next: decode `setText` single/short frames (10 0E … format) and UpdateList (0x121); a nicer
  progress-bar widget.
- **CLEANUP (top): affa3 debug spam.** With media rendering every ~300ms, `affa3_do_send`'s
  AFFA3_PRINT lines (`Sending packet #N`, `PARTIAL ack`, `affa3_do_send called`, `do_send totalLen`,
  `skipFuncReg …`) flood serial — ~66% of the channel. Gate/remove them (they're debug). Fits the
  "compact log" goal; not a blocker (heap healthy, 0 crashes).
- B3 (true closed loop): PC injects `@INJ (id|0x400) 74…/30 01 00…` to ACK over serial instead of
  self-ACK — needs the proxy serial-WRITE path fixed first (and the `affa3_do_send` wait loop to
  pump serial, since it currently blocks without reading input).
- **Goal:** reverse-engineer the **AFFA3NAV navigation screen** so iPhone ANCS/Apple-Maps drive
  turn-by-turn arrows. Record real CAN logs **in the car** and decode with this emulator.

**C ✅ — docs:** `README.md` Bluetooth fixed (central→peripheral), radio/display matrix added,
`display_type` table corrected (commit `a10d54d`). Deep README rewrite of the detail sections (still
use pre-rename class names `Affa2*`/`Affa3Nav*`) deferred — `CLAUDE.md` is authoritative.

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
- On **`main`** (top = merge `a1f109b`, PR #4). Tree clean. Local `main` == `origin/main`.
- Single remote: **`origin` = `Barbedx/MeganeCAN`** (default branch `main`). The session's work is
  all merged; the feature branch was deleted. New work: branch off `main`, PR back.
- Reference sandbox (proven BLE approach) lives at `../NimBLE-AMS-CNTRL-01`.

## Gotchas / invariants (don't regress)
- Flashing needs COM5 free → stop the proxy first.
- BLE callbacks never block / never GATT-write-with-response (deadlocks the host).
- Bench has no CAN bus → `busAlive()` gates TX; in the car, RX unlocks TX. Don't transmit unguarded
  (TWAI assert reboot loop).
- WiFi+BLE one radio: keep `setSleep(true)` AND the relaxed conn params, or the dashboard starves.
- Keep memory lean: no big RAM buffers / `String` churn in HTTP; the device runs near the heap edge.
- **Serial INPUT from the proxy doesn't reach the firmware** yet: `tools/serial_proxy.py` `/send`
  writes to COM5 (the `>>> cmd` marker logs) but SerialCommands never reacts (even `cb`/`pp`). The
  firmware OUTPUT is fine. Suspect pyserial write-on-Windows / DTR-RTS / flush. Blocks the serial
  `@INJ`/`@EMU`/`@KEY` path → use the HTTP equivalents (`/api/emu`, `/emulate/key`). Fix this to
  enable the true closed-loop emulator (B3). Also `affa3_do_send`'s 2s wait loop doesn't pump serial.
