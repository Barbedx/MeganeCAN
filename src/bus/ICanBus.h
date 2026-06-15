#pragma once
#include "Frame.h"

// RX callback signature. A plain function pointer (NOT std::function) to avoid heap
// allocation / RTTI on the RAM-tight ESP32; `ctx` carries instance state in lieu of
// a closure.
using RxHandler = void (*)(const Frame&, void* ctx);

// The CAN bus as the logic layer sees it. HwCanBus wraps the real controller;
// later phases add LoopbackCanBus (in-memory, host tests) and ReplayCanBus
// (recorded .canlog playback). Everything above this speaks only Frame.
struct ICanBus {
    virtual ~ICanBus() = default;
    virtual bool send(const Frame& f) = 0;          // false if dropped (e.g. !isLive)
    virtual void onReceive(RxHandler h, void* ctx) = 0;
    virtual bool isLive() const = 0;                // replaces CanUtils::busAlive()
    virtual void poll() {}                          // pump for loopback/replay; no-op on hw
};
