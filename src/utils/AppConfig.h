#pragma once
#include <Arduino.h>

// Config cached in RAM, read from NVS once at boot. The dashboard config getters
// previously opened NVS ("config", read-only) on every request, which both churned
// the heap and spammed "nvs_open failed: NOT_FOUND" when the namespace did not exist
// yet (fresh flash). Load() creates the namespace so those errors stop, and the
// getters now read these RAM fields instead of touching NVS.
namespace AppConfig
{
    // NVS schema version — bump when the "config" layout changes; Load() records it
    // and can migrate. v1 added the provisioned flag.
    static constexpr uint32_t SCHEMA_VERSION = 1;

    extern String displayType; // "carminat" | "updatelist" | "updatelist_menu"
    extern String btMode;      // "ams" | "keyboard" | "both"
    extern bool   autoTime;
    extern bool   elmEnabled;
    extern bool   skipFuncReg;

    extern uint32_t schemaVersion; // version found in NVS at boot
    extern bool     provisioned;   // false on a fresh device -> first-run setup needed

    void Load(); // read NVS "config" into RAM once (call at boot)

    // Typed views over the string settings (compile-time-safe; no string typos).
    enum class DisplayKind : uint8_t { Carminat, UpdateListSeg, UpdateListLcd, Unknown };
    // Both = iPhone AMS client and head-unit HID server on two simultaneous links.
    enum class BtKind      : uint8_t { Ams, Keyboard, Both };
    DisplayKind displayKind();
    BtKind      btKind();
    const char* displayKindStr(DisplayKind k);
    const char* btKindStr(BtKind k);
    bool        isBtMode(const char* s); // valid NVS value?

    // Which BLE roles the current mode runs. Prefer these over comparing btMode
    // strings at call sites.
    inline bool amsActive() { return btKind() != BtKind::Keyboard; }
    inline bool hidActive() { return btKind() != BtKind::Ams; }

    bool isUnconfigured();   // a fleet device that hasn't been set up yet
    void markProvisioned();  // persist provisioned=true after first-run setup
}
