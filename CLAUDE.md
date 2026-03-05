# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based CAN-bus companion for Renault Mégane 2 infotainment/multimedia displays. The firmware connects an ESP32-C3 to the vehicle CAN bus and communicates with the OEM "AFFA3" display unit via CAN frames, bridging phone media controls (Apple Media Service over BLE), providing OBD/ELM327 diagnostics via a WiFi ELM327 adapter, and exposing a web UI for configuration and OTA updates.

## Build & Flash (PlatformIO)

```bash
# Build firmware
pio run

# Upload over USB
pio run -t upload

# Serial monitor (115200 baud)
pio device monitor

# OTA upload (requires device on same network)
pio run -t upload --upload-port <device-ip>
```

Target: `esp32-c3-devkitm-1` (ESP32-C3 mini). CAN pins: RX=GPIO3, TX=GPIO4.

## Secrets

`src/secrets.h` is intentionally untracked (`git update-index --assume-unchanged src/secrets.h`). It defines:
- `Soft_AP_WIFI_SSID` — soft AP name for the web UI
- `Soft_AP_WIFI_PASS` — soft AP password

## Architecture

### Display Abstraction Layer

`IDisplay` (pure interface) → `AffaDisplayBase` (CAN send logic, sync state machine) → concrete implementations:

- **`Affa3Display`** — "Update" mode display. Supports `setText`, `setState`, `setTime`. No menu/key support.
- **`Affa3NavDisplay`** — "Carminat/Nav" mode display. Full feature set: menus, key press handling, highlight, info panels, BLE keyboard for AMS.

`initDisplay()` in `main.cpp` reads the `display_type` key from NVS (`Preferences`) and instantiates the correct variant at runtime. All other subsystems take `IDisplay&`.

### CAN Bus Communication

CAN frames are received via `gotFrame()` callback registered with `CAN0.setGeneralCallback()`. Each display variant implements `recv(CAN_FRAME*)` to handle inbound frames (key presses, sync responses). Outbound display commands are 8-byte CAN frames sent via `affa3_send()` / `affa3_do_send()` in `AffaDisplayBase`.

### ELM327 / OBD Diagnostics

`MyELMManager` connects via TCP WiFi to a "V-LINK" ELM327 adapter (IP `192.168.0.10:35000`). It cycles through a combined `PidPlan` (querying ECU headers 7E0, 743, 744, 745, 74D) using a non-blocking state machine (`tick()` called from `loop()`). Results are cached in `valueCache` (keyed by `shortName`) and exposed as JSON via `snapshotJson()`.

PID plans live in `src/ElmManager/PidPlan_*.h`. Each plan defines `MetricDef` entries with letter-indexed byte extraction helpers (`U8`, `U16`, `getBIT` from `DiagPlanCommon.h`).

### HTTP Server & Web UI

`HttpServerManager` wraps PsychicHttp. It holds references to `IDisplay` and `Preferences`, and optionally an `MyELMManager*` (attached via `attachElm()`). Routes serve the configuration UI and OTA (via ElegantOTA).

`DisplayCommands::Manager` (`src/commands/DisplayCommands.*`) translates HTTP requests into `IDisplay` calls.

### WiFi Dual-Mode

The device runs `WIFI_AP_STA`: AP mode hosts the web UI, STA mode connects to the ELM327 WiFi adapter ("V-LINK"). WiFi STA connection is managed with a retry loop in `loop()`.

### Serial Commands

`SerialCommands` processes commands over USB serial:
- `e` / `d` — enable/disable display
- `st HHMM` — set time
- `msr <text> [delay_ms]` — scroll right
- `msl <text> [delay_ms]` — scroll left

### NVS Persistence

`Preferences` namespaces used:
- `"config"` — `display_type` (string: `"affa3"` or `"affa3nav"`)
- `"display"` — `autoRestore` (bool), `lastText` (string), `welcomeText` (string)

### Key Data Types

- `AffaCommon::AffaKey` — enum for remote button events (src/display/AffaCommonConstants.h)
- `AffaCommon::SyncStatus` — bitmask tracking display sync handshake state
- `PidPlan` / `MetricDef` — OBD query definition (src/ElmManager/DiagPlanCommon.h)
- `MenuItem` / `Field` — nav menu item model (src/display/Affa3Nav/Menu/)

### merge-bin.py

Post-build script (referenced via `extra_scripts`) that merges bootloader, partition table, and app binaries into a single flashable `.bin` for distribution.
