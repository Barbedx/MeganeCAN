#pragma once
#include <stdint.h>
#include "IKeySink.h"

// Routes SWC key events to the configured sinks (ARCHITECTURE-V2 §6.2). Two key
// classes with separate sink masks — this is what expresses the old "both" mode
// exactly: transport control to the iPhone (AMS), volume to the head unit (HID).
// Portable (no Arduino) — native tests drive it with fake sinks.
class KeyRouter {
public:
    // Sink identity bits (shared with the AppConfig key_sinks packing).
    static constexpr uint8_t SINK_AMS = 0x01;
    static constexpr uint8_t SINK_HID = 0x02;
    static constexpr uint8_t SINK_HU  = 0x04;  // LinkProto KEY_EVT (P1+)
    static constexpr uint8_t SINK_MASK = 0x07;

    // NVS key_sinks packed byte:
    //   bits0-2 = transport sink mask, bit3 = volume hold-only,
    //   bits4-6 = volume sink mask.
    static constexpr uint8_t HOLD_ONLY_BIT = 0x08;

    static uint8_t pack(uint8_t transportMask, uint8_t volumeMask, bool volumeHoldOnly);
    static void unpack(uint8_t packed, uint8_t& transportMask, uint8_t& volumeMask,
                       bool& volumeHoldOnly);

    // Migration default mirroring the legacy bt_mode behavior exactly:
    //   "ams"      -> transport AMS,  volume AMS,  hold-only
    //   "keyboard" -> transport HID,  volume HID,  hold-only
    //   "both"     -> transport AMS,  volume HID,  immediate
    static uint8_t defaultsForBtMode(const char* btMode);

    void bind(uint8_t sinkBit, IKeySink* sink);   // register the impl for SINK_x
    void configure(uint8_t packed);
    uint8_t packed() const { return _packed; }

    // Dispatch one key event. Always returns true (the display's keyHandler
    // contract: the key was consumed).
    bool route(AffaCommon::AffaKey key, bool isHold);

    static bool isVolumeKey(AffaCommon::AffaKey k) {
        return k == AffaCommon::AffaKey::VolumeUp || k == AffaCommon::AffaKey::VolumeDown;
    }

private:
    IKeySink* _sinks[3] = {};     // indexed by bit position (AMS=0, HID=1, HU=2)
    uint8_t _transportMask  = SINK_AMS;
    uint8_t _volumeMask     = SINK_AMS;
    bool    _volumeHoldOnly = true;
    uint8_t _packed         = SINK_AMS | HOLD_ONLY_BIT | (SINK_AMS << 4);
};
