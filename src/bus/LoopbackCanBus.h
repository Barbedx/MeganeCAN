#pragma once
#include "ICanBus.h"

// In-memory CAN bus for host tests (and bench radio<->virtual-display loops). send()
// enqueues into a small static ring; poll() drains it, fanning each frame out to the
// registered onReceive handler so a radio-side sender and a virtual display can be
// wired to the SAME bus with no hardware. Always "live" so the TX gate never blocks.
//
// Header-only and dependency-free (Frame + ICanBus only) so it compiles unchanged on
// the native host and on target. Function-pointer callback — no heap, no std::function.
class LoopbackCanBus : public ICanBus {
public:
    static constexpr int CAPACITY = 32;

    bool send(const Frame& f) override {
        if (_count >= CAPACITY) {
            _dropped++;
            return false;           // ring full: drop (caller should poll() more often)
        }
        _ring[_tail] = f;
        _tail = (_tail + 1) % CAPACITY;
        _count++;
        return true;
    }

    void onReceive(RxHandler h, void* ctx) override { _rx = h; _rxCtx = ctx; }

    bool isLive() const override { return true; }

    // Drain every queued frame to the handler. Re-entrancy safe for the common case
    // where a handler send()s in response: those land at the tail and are delivered
    // on the next poll(), not this one (we snapshot the count up front).
    void poll() override {
        int n = _count;
        while (n-- > 0 && _count > 0) {
            Frame f = _ring[_head];
            _head = (_head + 1) % CAPACITY;
            _count--;
            if (_rx) _rx(f, _rxCtx);
        }
    }

    // Test helpers.
    int  pending() const  { return _count; }
    int  dropped() const  { return _dropped; }
    void reset() { _head = _tail = _count = _dropped = 0; }

private:
    Frame     _ring[CAPACITY];
    int       _head = 0;
    int       _tail = 0;
    int       _count = 0;
    int       _dropped = 0;
    RxHandler _rx = nullptr;
    void*     _rxCtx = nullptr;
};
