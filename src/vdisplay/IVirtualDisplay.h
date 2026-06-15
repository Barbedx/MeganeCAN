#pragma once
#include "../bus/Frame.h"
#include "../bus/ICanBus.h"
#include "../bus/IClock.h"
#include "../affa/ScreenModel.h"

// A faithful twin of a connected AFFA3 display. It consumes the frames a radio sends
// (text/menu/sync) and behaves like the real panel:
//   * PASSIVE   — decode/render only (the model exposed via screen()).
//   * EMULATION — additionally auto-ACK received frames and send sync replies + keys.
// Talks ONLY to ICanBus + IClock, so it runs on hardware (HwCanBus) or on the native
// host (LoopbackCanBus) with no #ifdefs.
struct IVirtualDisplay {
    virtual ~IVirtualDisplay() = default;

    virtual void begin(ICanBus& bus, IClock& clock) = 0;
    virtual void onCanRx(const Frame& f) = 0;          // a frame arrived from the radio
    virtual void pressKey(uint16_t code, bool hold) = 0; // TX a key (display -> radio)
    virtual const ScreenModel& screen() const = 0;
};
