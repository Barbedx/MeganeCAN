#pragma once
#include <PsychicHttp.h>
#include "WireLink.h"
#include "freertos/FreeRTOS.h"

// WebSocket link: broadcasts the WireProto stream to connected phones/browsers and
// feeds their inbound messages back as commands. One shared broadcast socket on
// /canstream (bidirectional — also fixes the broken serial-input path).
//
// Stable / fast / transparent design:
//   * Producers (@TX from the loop task, @RX from the CAN task) only APPEND lines to
//     a buffer under a short critical section — never touch the socket, never block.
//   * loop() flushes the whole buffer in ONE sendAll() at a fixed cadence (~25 Hz)
//     from a single task — batched (fast), race-free (stable).
//   * Lines accumulate losslessly within the buffer (no per-frame time sampling), so
//     the client sees every frame — transparent; only a genuine overflow drops, and
//     that is counted. The 0x3AF/0x3CF sync chatter is still skipped as pure noise.
//   * Everything is dropped while no client is connected.
class WsWireLink : public WireLink {
public:
    // Register the handler + callbacks on the shared server (call in begin()).
    void attach(PsychicHttpServer& server, const char* path = "/canstream");

    // Flush the batched buffer to clients. Call once per main loop().
    void loop();

    // WireLink
    void emitLine(const char* line) override;
    void onCommand(CommandCb cb, void* ctx) override { _cb = cb; _ctx = ctx; }
    bool connected() const override { return _clients > 0; }

    // Append a received CAN frame as "@RX <id> <bytes>" (skips sync chatter).
    void emitRxFrame(uint32_t id, const uint8_t* data, uint8_t len);

    uint32_t dropped() const { return _dropped; }

private:
    static constexpr int      BUF_SIZE  = 2048;   // accumulate ~40 lines between flushes
    static constexpr int      FLUSH_HI  = 1400;   // flush early once this full
    static constexpr uint32_t FLUSH_MS  = 40;     // ~25 Hz cadence

    void append(const char* s, int n);

    PsychicWebSocketHandler _ws;
    volatile int  _clients = 0;
    CommandCb     _cb = nullptr;
    void*         _ctx = nullptr;

    char     _buf[BUF_SIZE];
    char     _tx[BUF_SIZE + 1];      // double buffer (+1 for NUL): flush copies here, then sendAll
    volatile int _len = 0;
    volatile uint32_t _dropped = 0;
    uint32_t _lastFlush = 0;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};
