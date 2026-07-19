#pragma once
#include "IKeySink.h"

// SWC keys -> Apple Media Service remote commands (the iPhone is the audio
// source, so transport control goes straight to it instead of round-tripping
// through a head unit's AVRCP). Target-only.
class AmsKeySink : public IKeySink {
public:
    void onKey(AffaCommon::AffaKey key, bool isHold) override;
};
