# MeganeCAN

ESP32-based CAN-bus companion for Renault infotainment / multimedia displays.

This project connects an **ESP32** to the car’s **CAN bus** and acts like a “smart head unit sidecar”: it can **emulate button presses**, **render custom screens/menus** on the OEM display, and **bridge your phone media controls** (Apple Media Service) back into the car experience. It also exposes a **web UI** for configuration and **OTA updates**.

🇺🇦 When you use this project, you automatically support Ukraine — keep that in mind. If that’s forbidden in your country, remove yourself from this page, at least.


> ⚠️ Safety: this is a hobby project for off-road / testing use. Don’t interact with menus while driving.

---

## What it can do

### CAN-bus integration
- Connects ESP32 to the car CAN bus via an external CAN transceiver.
- Listens to OEM packets and reacts to relevant frames.
- **Emulates key/button presses** (e.g., sending the same CAN messages the multimedia keyboard would send).

### Display features
- Shows a **custom “test menu” / debug UI** on the OEM display.
- Supports multiple display variants (currently **Update** and **Carminat** modes).

### Bluetooth phone media bridge (Apple Media Service)
- ESP32 pairs with an iPhone using **AMS (Apple Media Service)**.
- Can:
  - Control **music playback** (play/pause/next/prev).
  - Control **volume**.
  - Read **now playing text** (track/artist/etc.) and show it on the display (where supported).
  - Sync **time** from the phone.

### Web UI + OTA
- Built-in web server with HTML configuration pages.
- Configure device/display parameters from a browser.
- **OTA updates** supported (firmware update from the web UI).

### Roadmap / next steps
- **ELM327 / OBD integration** planned (next step: bridge or coexist with ELM data flow).

---

## Repository layout (high level)

- `src/` – main firmware sources
- `lib/` – local / custom libraries
- `include/` – headers
- `platformio.ini` – PlatformIO build configuration
- `partitions_ota.csv` – partition table tuned for OTA workflow
- `merge-bin.py` – helper script (e.g., merging binaries / packaging)
- `notes/`, `logs/` – project notes and capture logs

---

## Hardware

Minimum typical setup:
- **ESP32** development board
- **CAN transceiver** (e.g., SN65HVD230 / MCP2551 / TJA1050 — depends on your vehicle bus)
- Wiring to **CAN-H / CAN-L** and **GND** (and power, ideally fused)

Optional:
- Dedicated power supply / buck converter for stable 5V → 3.3V (ESP32 boards usually provides) 
- Case + automotive-grade connectors

> Tip: Use proper termination only where appropriate. Don’t double-terminate an already-terminated bus.

---

## Build & flash (PlatformIO)

This project is set up for **PlatformIO** (see `platformio.ini`).

Typical workflow:

```bash
# Build
pio run

# Upload over USB
pio run -t upload

# Serial monitor
pio device monitor
