#pragma once
#include "IKeySink.h"

// SWC keys -> BLE HID consumer-control presses (the head unit drives the
// amplifier, so volume lands here in "both"-style routing). Target-only.
class HidKeySink : public IKeySink {
public:
    void onKey(AffaCommon::AffaKey key, bool isHold) override;
};
