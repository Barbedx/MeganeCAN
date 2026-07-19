#pragma once
#include "IKeySink.h"

// SWC keys -> LinkProto KEY_EVT -> VH board -> Raise 0x20 to the DUDU7
// (ARCHITECTURE-V2 §6.2). The AffaKey code travels raw; the AffaKey->RAV4
// mapping lives on VH (the HU profile owns its own codes). Emits press then
// release, matching the Raise key state machine.
class UartCanboxSink : public IKeySink {
public:
    void onKey(AffaCommon::AffaKey key, bool isHold) override;
};
