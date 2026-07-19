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
    // and can migrate. v1 added the provisioned flag; v2 (ARCHITECTURE-V2 P0) added
    // media_source / key_sinks / link_enabled, defaults preserving v1 behavior.
    static constexpr uint32_t SCHEMA_VERSION = 2;

    extern String displayType; // "carminat" | "updatelist" | "updatelist_menu"
    extern String btMode;      // "ams" | "keyboard" | "both"
    extern bool   autoTime;
    extern bool   elmEnabled;
    extern bool   skipFuncReg;

    // --- v2 (dual-board gateway) ---
    extern String  mediaSource; // "ams" | "hu" | "auto" (auto: HU wins while live)
    // Packed key routing byte (see KeyRouter::pack/unpack): bits0-2 transport
    // sinks, bit3 volume hold-only, bits4-6 volume sinks (1=AMS, 2=HID, 4=HU).
    // While the NVS key is unset this is re-derived from bt_mode on every boot,
    // so changing bt_mode keeps driving key behavior exactly as it did in v1.
    extern uint8_t keySinks;
    extern bool    linkEnabled; // inter-board LinkProto endpoint (P1+)

    extern uint32_t schemaVersion; // version found in NVS at boot
    extern bool     provisioned;   // false on a fresh device -> first-run setup needed

    void Load(); // read NVS "config" into RAM once (call at boot)

    // Typed views over the string settings (compile-time-safe; no string typos).
    enum class DisplayKind : uint8_t { Carminat, UpdateListSeg, UpdateListLcd, Unknown };
    // Both = iPhone AMS client and head-unit HID server on two simultaneous links.
    enum class BtKind      : uint8_t { Ams, Keyboard, Both };
    enum class MediaSourceKind : uint8_t { Ams, Hu, Auto };
    DisplayKind displayKind();
    BtKind      btKind();
    MediaSourceKind mediaSourceKind();
    bool        isMediaSource(const char* s); // valid NVS value?
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
