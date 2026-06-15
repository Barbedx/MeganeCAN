#pragma once
#include <PsychicHttp.h>
#include "WireLink.h"

// WebSocket link: broadcasts the WireProto stream to connected phones/browsers and
// feeds their inbound messages back as commands. One shared broadcast socket on
// /canstream (bidirectional — fixes the broken serial-input path too).
//
// Memory discipline (the device is RAM-tight, see CLAUDE.md):
//   * drop everything when no client is connected (no formatting, no sendAll);
//   * @TX (display frames, low rate) is sent losslessly so screen replay is exact;
//   * @RX (the raw car bus, 100+/s) is sampled to ~10 Hz and skips the 0x3AF/0x3CF
//     sync chatter, so a flood never starves the WS pump.
class WsWireLink : public WireLink {
public:
    // Register the handler + callbacks on the shared server. Call before the first
    // client connects (during HttpServerManager::begin).
    void attach(PsychicHttpServer& server, const char* path = "/canstream");

    // WireLink
    void emitLine(const char* line) override;
    void onCommand(CommandCb cb, void* ctx) override { _cb = cb; _ctx = ctx; }
    bool connected() const override { return _clients > 0; }

    // Mirror a received CAN frame as "@RX <id> <bytes>" (rate-limited; see above).
    void emitRxFrame(uint32_t id, const uint8_t* data, uint8_t len);

    // Minimum gap between @RX samples (ms). Default ~10 Hz.
    void setRxIntervalMs(uint32_t ms) { _rxIntervalMs = ms; }

private:
    PsychicWebSocketHandler _ws;
    volatile int _clients = 0;
    CommandCb    _cb = nullptr;
    void*        _ctx = nullptr;
    uint32_t     _lastRxMs = 0;
    uint32_t     _rxIntervalMs = 100;
};
