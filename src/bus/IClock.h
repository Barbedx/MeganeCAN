#pragma once
#include <stdint.h>

// Time seam. On target, ArduinoClock wraps millis()/delay(); native tests use a
// FakeClock that is advanced manually so the per-frame 2s ACK wait in
// AffaDisplayBase::affa3_do_send becomes instant + deterministic. Function-pointer
// / interface style (no std::function) to stay cheap on a RAM-tight ESP32.
struct IClock {
    virtual ~IClock() = default;
    virtual uint32_t millis() const = 0;
    virtual void delayMs(uint32_t ms) = 0;
};
