# BLE migration plan — adopt the peripheral AMS/ANCS approach

Status: **PLAN ONLY — do not change code yet.** Source of the new approach:
`../NimBLE-AMS-CNTRL-01` (published reference, verified on ESP32‑C3 hardware).

## Why
MeganeCAN's current BLE is **central mode**: it scans for an iPhone advertising the **Spotify
service UUID** `3e1d50cd-…` and connects to it. Spotify stopped advertising that UUID, so the
phone is no longer found. The reference project replaces this with a **peripheral** model that
doesn't depend on any app-specific advertisement, fixes silent reconnection (bonding), and adds
ANCS notifications. We port that here.

## End state
- BLE: ESP32 advertises as peripheral **"MeganeCAN"**; user pairs from **iOS Settings → Bluetooth**;
  ESP32 takes a GATT client over the inbound connection (`NimBLEServer::getClient`) → AMS + ANCS + CTS.
  Bonding → silent auto-reconnect. No scanning, no Spotify-UUID dependency.
- ANCS notifications rendered on the Carminat display (new page), like the now-playing screen.
- WiFiManager (home WiFi + mDNS) added without breaking the ELM327 / V-LINK STA usage.
- Single PsychicHttp server (existing `HttpServerManager`) — fold in any new routes; no second server.

---

## What carries over (reference → MeganeCAN)

Copy these modules from `NimBLE-AMS-CNTRL-01/` (they are self-contained):
- `bluetooth.{h,cpp}` — **replaces** the existing central-mode files.
- `apple_media_service.{h,cpp}` — newer (has `CurrentElapsedTime()` extrapolation).
- `apple_notification_service.{h,cpp}` — **new** (ANCS history).
- `current_time_service.{h,cpp}` — essentially unchanged; verify diffs.
- `wifi_manager.{h,cpp}` — adapt (see Phase 3).
- `web_portal.cpp` — **do not copy wholesale**; MeganeCAN already has `HttpServerManager`. Lift the
  AMS/ANCS/Wi-Fi JSON + routes into it.

### Public API contract MeganeCAN already depends on (must keep working)
From `main.cpp` / `CarminatDisplay`:
- `Bluetooth::Begin(name)`, `Bluetooth::Service()`, `Bluetooth::IsConnected()`, `Bluetooth::IsTimeSet()`, `Bluetooth::ClearBonds()`.
- `Bluetooth::GetStatusText()` and `GetStatusJson()` — **the reference lacks these; re-add them** (status now: "Advertising — pair from iPhone", "Securing…", "Connected").
- `AppleMediaService::RegisterForNotifications(cb, level)`, `GetMediaInformation()`, `SendRemoteCommand()` / `Toggle()/NextTrack()/PrevTrack()/VolumeUp()`.
- `CurrentTimeService::StartTimeService(client, &time)`.

### API that DISAPPEARS in the peripheral model (update callers)
- `Bluetooth::SelectNext()/SelectPrev()/ConnectSelected()` — there is no candidate list anymore
  (you pick the ESP32 by name in iOS). In `HandleKey()` (main.cpp, disconnected branch) remove the
  RollUp/RollDown/Pause → Select/Connect mapping; when disconnected there's simply nothing to steer.
- `HttpServerManager` `/bt/try` (connect by index) becomes a no-op/removed.

---

## Phase 1 — BLE core swap
1. Replace `src/bluetooth.{h,cpp}`, `src/apple_media_service.{h,cpp}`, `src/current_time_service.{h,cpp}`
   with the reference versions. Add `src/apple_notification_service.{h,cpp}`.
2. Re-add `GetStatusText()`/`GetStatusJson()` to `Bluetooth` (used by `CarminatDisplay::tickMedia`
   disconnected screen and the web `/api/bt`).
3. `Bluetooth::Begin("MeganeCAN")` — name shows in iOS Settings. Keep the existing **background-task
   init** (`xTaskCreate`) — the peripheral `Begin()` is non-blocking, so it can also run inline; either is fine.
4. Keep the existing `onDataUpdateCallback` → `display->setMediaInfo(info)` bridge.
5. Decide elapsed-time source: the reference computes it in `AppleMediaService::CurrentElapsedTime()`;
   `CarminatDisplay::tickMedia()` *also* interpolates (`mLastPlaybackInfoMs`). **Pick one** — prefer the
   AMS-module helper and simplify the display to call it. (Field name differs: reference uses an internal
   `gPlaybackInfoMs`; MeganeCAN's struct exposes `mLastPlaybackInfoMs` — reconcile.)
6. Preserve the threading invariant (see Risks) — already satisfied by the reference's ANCS `Process()`.

## Phase 2 — ANCS on the display
1. Wire `AppleNotificationService::StartNotificationService(client)` in `Bluetooth`'s connect path
   (already done in reference) and call `AppleNotificationService::Process()` from `loop()` while connected.
2. Add a **Notifications page** to `CarminatDisplay` (mirror the media screen): a new `IPage`/menu view
   that renders `AppleNotificationService::GetRecent()` (newest first: title / message / category·app),
   reachable via a steering-wheel key or auto-popup on a new notification. Use the existing
   `ScrollEffect` for long lines.
3. Optional: incoming-call category → prominent popup; later, Perform Notification Action (answer/decline)
   via ANCS Control Point.

## Phase 3 — WiFi reconciliation (the careful part)
Single STA radio is currently dedicated to the **V-LINK ELM327** adapter (open SSID `V-LINK`, static
`192.168.0.151`, gw `192.168.0.10`). Home-WiFi STA and V-LINK STA are mutually exclusive.

**Recommended (Option B): piggyback on the existing `elm_enabled` flag.**
- `elm_enabled == true`  → STA joins `V-LINK` (current behavior), AP stays up for the dashboard. No mDNS/home.
- `elm_enabled == false` → `WiFiManager` joins **home WiFi** (NVS creds) + **mDNS `meganecan.local`**;
  AP stays up as fallback/config.
- AP is always on (`WIFI_AP_STA`) using `secrets.h` SSID/pass → pass those to `WiFiManager::Begin()`.
- Keep `WiFi.setSleep(true)` (radio coexistence with BLE + CAN).
- Caveat: `WiFiManager::Begin()` in the reference blocks up to ~12 s waiting for STA. In MeganeCAN's
  `setup()` that delays boot — make the home-WiFi connect **non-blocking** (kick off, check in `loop()`),
  matching the existing one-shot `connectToElm()` pattern.

(Option A: an explicit `wifi_mode` NVS key `elm|home|ap` — only if you want all three as first-class choices.)

## Phase 4 — Web server merge
- Do **not** instantiate a second `PsychicHttpServer`. Add routes to `HttpServerManager`:
  - reuse existing `/api/bt`, `/clearbonds`, `/forgetdevice`;
  - add ANCS to the status JSON (`GetRecent()`), and media now-playing/controls if not already present;
  - if adopting WiFiManager config via web: add `/api/wifi/scan`, `/api/wifi/networks`, `/api/wifi/save`
    (lift the handlers from `web_portal.cpp`).
- Keep ElegantOTA as-is.

## Phase 5 — coexistence & build
- Confirm init order: BLE up (or task queued) **before** `softAP`, `WiFi.setSleep(true)` early — already done.
- **Flash budget:** MeganeCAN is much larger (CAN + ELM + display + OTA). The reference firmware is
  ~1.1 MB; verify the combined build fits `app0` (1.375 MB in `partitions_ota.csv`). If tight, trim
  debug logging / features.
- Single-core C3 now juggles CAN + WiFi + BLE + HTTP — watch for the deadlock class below.

---

## Risks / invariants to carry over
1. **BLE callbacks never block / never do GATT write-with-response.** A blocking write inside a notify
   callback deadlocks the NimBLE host (buffers exhaust → `Failed to allocate buffer, retrying`, BLE+web
   wedge). Outbound writes go on the loop task (ANCS `Process()`). This bit us in the reference — keep it.
2. **Advertising:** name in the *primary* packet (else iOS Settings hides it) + AMS UUID as a *solicited*
   service (AD type `0x15`; `addData()` has no length byte → build `[0x11][0x15][16 LE]`). NimBLE issue #1033.
3. **`secureConnection()` before service discovery**, retried until it succeeds.
4. **`setMTU(247)`** for ANCS message bodies.
5. We host **no** GATT services — fine; scanners "discovering forever" is expected.

## File-by-file change list (summary)
- `src/bluetooth.{h,cpp}` — replace; re-add `GetStatusText/GetStatusJson`; advertise "MeganeCAN".
- `src/apple_media_service.{h,cpp}` — replace.
- `src/apple_notification_service.{h,cpp}` — add.
- `src/current_time_service.{h,cpp}` — replace (diff-check).
- `src/wifi_manager.{h,cpp}` — add; make STA connect non-blocking; gate on `elm_enabled`.
- `src/main.cpp` — drop Select/Connect key mapping; wire ANCS `Process()`; wire WiFiManager; pick elapsed source.
- `src/display/Carminat/*` — add Notifications page; simplify media elapsed to `CurrentElapsedTime()`.
- `src/server/HttpServerManager.{h,cpp}` — add ANCS to status JSON + (optional) Wi-Fi config routes; drop `/bt/try`.
- `BleMediaKeyboard.h` — unchanged (keyboard mode kept as the non-AMS alternative; relevant for the Android/HID idea).

## Test checklist
- [ ] Pair "MeganeCAN" from iOS Settings; reconnect after reboot with no prompt.
- [ ] Now-playing on Carminat screen; progress bar advances; steering keys = play/pause/next/prev/vol.
- [ ] Notifications page shows recent items; clears on phone dismissal.
- [ ] CTS sets the clock once per connection.
- [ ] `elm_enabled=true` → OBD data still flows (V-LINK STA); `false` → home WiFi + `meganecan.local`.
- [ ] OTA via ElegantOTA still works.
- [ ] No `Failed to allocate buffer` under notification load; CAN unaffected.

## Rollback
Branch before starting; the old BLE files are isolated, so `git revert`/checkout restores central mode.
