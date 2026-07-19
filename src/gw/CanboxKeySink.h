#pragma once
#include "../keys/IKeySink.h"
#include "../vh/CanboxEmitter.h"

// SWC keys -> the LOCAL Raise canbox emitter (GW owns the DUDU7 UART, so the
// old UartCanboxSink link hop disappears). Press then release, matching the
// Raise key state machine; unmapped keys are dropped silently.
class CanboxKeySink : public IKeySink {
public:
    explicit CanboxKeySink(Vh::CanboxEmitter& e) : _e(e) {}
    void onKey(AffaCommon::AffaKey key, bool isHold) override
    {
        uint8_t code = Vh::CanboxEmitter::rav4CodeForAffa(AffaCommon::to_uint16(key));
        if (!code) return;
        _e.sendKey(code, isHold ? 2 : 1);
        _e.sendKey(code, 0);
    }
private:
    Vh::CanboxEmitter& _e;
};
