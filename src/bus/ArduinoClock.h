#pragma once
#include <Arduino.h>
#include "IClock.h"

// Target clock: thin wrapper over the Arduino millis()/delay(). Behavior is
// identical to calling them directly — this only exists so the logic layer can be
// handed a FakeClock on the native host.
struct ArduinoClock : IClock {
    uint32_t millis() const override { return ::millis(); }
    void delayMs(uint32_t ms) override { ::delay(ms); }
};

// Process-wide default clock used by logic that hasn't been handed an explicit
// one. inline => a single shared instance across all translation units. Native
// tests substitute a FakeClock via the relevant setClock() seam.
inline IClock& defaultClock() {
    static ArduinoClock c;
    return c;
}
