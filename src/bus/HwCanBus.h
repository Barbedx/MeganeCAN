#pragma once
#include <can_common.h>
#include <esp32_can.h>
#include "ICanBus.h"
#include "BusTap.h"

// The hardware CAN bus. This is the ONE place that converts between the driver's
// CAN_FRAME and the portable Frame. It owns:
//   * the live-bus gate (moved here from CanUtils::busAlive) — TX is suppressed
//     until RX traffic proves a transceiver + live bus is present, which keeps the
//     bench board (no transceiver) out of the TWAI bus-off reboot loop;
//   * the TX/RX tap fan-out (e.g. the @TX serial mirror).
// A singleton because there is exactly one controller (CAN0) and the legacy
// CanUtils statics delegate here.
class HwCanBus : public ICanBus {
public:
    static HwCanBus& instance();

    bool send(const Frame& f) override;
    void onReceive(RxHandler h, void* ctx) override { _rx = h; _rxCtx = ctx; }
    bool isLive() const override;
    // poll() unused: the esp32_can general callback (ingest) drives RX.

    // Feed a raw driver frame in from the CAN0 general callback: refresh the live
    // gate, run RX taps, dispatch to the registered handler (if any).
    void ingest(const CAN_FRAME& f);

    void addTap(IBusTap* t);
    void noteRxActivity();   // refresh the live gate (legacy CanUtils delegation)

    static constexpr int MAX_TAPS = 4;

private:
    HwCanBus() = default;
    RxHandler _rx = nullptr;
    void*     _rxCtx = nullptr;
    IBusTap*  _taps[MAX_TAPS] = {};
    int       _tapCount = 0;
    volatile uint32_t _lastRxMs = 0;
    static constexpr uint32_t BUS_ALIVE_WINDOW_MS = 5000;
};
