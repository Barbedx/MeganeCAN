#pragma once
#include "MediaInfo.h"

// A provider of now-playing state (ARCHITECTURE-V2 §6.1). Implementations:
// AmsMediaSource (iPhone over BLE AMS), HuLinkMediaSource (DUDU7 head unit over
// LinkProto, P1+). Portable interface — function-pointer callback, no <functional>.
class IMediaSource {
public:
    using ChangeCb = void (*)(IMediaSource& source, void* ctx);

    virtual ~IMediaSource() = default;

    virtual void poll() {}                          // per-loop pump (optional)
    virtual const MediaInfo& current() const = 0;
    virtual bool active() const = 0;                // link up / source usable now
    virtual const char* statusText() const { return ""; }  // short line for the display
    virtual const char* name() const = 0;           // "ams" | "hu"

    void setChangeCallback(ChangeCb cb, void* ctx) { _cb = cb; _ctx = ctx; }

protected:
    void notifyChange() { if (_cb) _cb(*this, _ctx); }

private:
    ChangeCb _cb = nullptr;
    void*    _ctx = nullptr;
};
