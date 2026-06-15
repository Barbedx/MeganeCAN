#pragma once
#include "../bus/IClock.h"

// Test clock: time only moves when the test advances it. delayMs() does NOT sleep —
// it just jumps the virtual clock forward, so AffaDisplayBase's 2s per-frame ACK wait
// resolves instantly and deterministically (no wall-clock seconds burned in tests).
// Pair with an emu/self-ACK path or an injected ACK so the wait loop's exit condition
// is met before/at the advance.
struct FakeClock : IClock {
    uint32_t now = 0;

    uint32_t millis() const override { return now; }
    void delayMs(uint32_t ms) override { now += ms; }

    void advance(uint32_t ms) { now += ms; }
    void reset() { now = 0; }
};
