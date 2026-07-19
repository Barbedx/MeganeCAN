#pragma once
#include "../display/AffaCommonConstants.h"

// A destination for steering-wheel key events (ARCHITECTURE-V2 §6.2):
// AmsKeySink (AMS remote commands), HidKeySink (BLE HID consumer keys),
// UartCanboxSink (LinkProto KEY_EVT -> VH -> Raise 0x20, P1+).
class IKeySink {
public:
    virtual ~IKeySink() = default;
    // Transport keys (Pause/RollUp/RollDown) and volume keys (VolumeUp/VolumeDown);
    // hold-gating for volume happens in KeyRouter, not here.
    virtual void onKey(AffaCommon::AffaKey key, bool isHold) = 0;
};
