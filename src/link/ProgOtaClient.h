#pragma once
#include <stdint.h>
#include <stddef.h>

// The flasher side of ProgProto: streams a firmware image to the PEER board in
// stop-and-wait chunks and drives its OTA_BEGIN/END handshakes. One shared
// implementation for both roles (the C3 flashing its WROVER peer, the GW
// flashing its peripheral) — extracted from two near-identical state machines.
//
// Threading contract (same as the link services): begin()/write()/end() run in
// the HTTP task and block politely (vTaskDelay); service() + onStat() run in
// the loop task that owns the LinkPort. The chunk buffer is the handoff.
class ProgOtaClient {
public:
    using SendFn = bool (*)(uint8_t type, const uint8_t* payload, uint16_t len, void* ctx);
    void bind(SendFn send, void* ctx) { _send = send; _sctx = ctx; }

    // ---- loop task ----
    void service(uint32_t nowMs);
    // Feed an incoming OTA_STAT; returns true if it was ours (an op is active).
    bool onStat(const uint8_t* payload, uint16_t len);

    // ---- HTTP task ----
    bool begin(uint32_t size);              // peer erases its slot (may take seconds)
    bool write(const uint8_t* data, size_t len);
    bool end();                             // peer verifies + switches boot slot

    bool active() const { return _st != IDLE && _st != ERROR; }
    uint32_t progress() const { return _acked; }
    const char* error() const { return _err; }

    static constexpr uint16_t CHUNK = 112;  // 4B offset + data <= link payload

private:
    enum St : uint8_t { IDLE, BEGIN_REQ, RUN, END_REQ, ERROR };
    void fail(const char* what, uint32_t detail);

    SendFn _send = nullptr;
    void* _sctx = nullptr;

    volatile St _st = IDLE;
    volatile bool _endAcked = false;
    uint32_t _size = 0;
    uint32_t _nextOff = 0;                  // next offset to transmit
    volatile uint32_t _acked = 0;           // bytes the peer confirmed
    volatile bool _awaitAck = false;
    uint32_t _lastTxMs = 0;

    uint8_t _buf[2048];
    volatile uint16_t _bufLen = 0;          // handler fills, service consumes
    uint16_t _bufOff = 0;

    char _err[32] = "";
};
