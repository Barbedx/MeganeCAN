#pragma once
#include <stdint.h>
#include "LinkCodec.h"
#include "LinkLock.h"
#include "../bus/IClock.h"

// Byte-stream seam so LinkPort is portable: HardwareSerial on target, an
// in-memory loopback pair in the native tests. Both ends non-blocking.
struct ILinkStream {
    virtual ~ILinkStream() = default;
    virtual int read(uint8_t* buf, int maxLen) = 0;         // bytes read (0 = none)
    virtual int write(const uint8_t* buf, int len) = 0;     // bytes accepted
};

// One end of the inter-board link (ARCHITECTURE-V2 §4). Non-blocking byte
// pump: service() drains RX into the decoder, runs the 1Hz PING / 3s
// peer-timeout heartbeat, and feeds the priority TX queue to the stream. A
// full queue drops the lowest-priority queued frame (LOG first, then
// RAW_FRAME/SIG_BATCH) — control traffic and keys always fit.
//
// Threading: send() is task-safe (slot claim/publish under LinkLock) so any
// task may enqueue — CAN-callback keys, BLE media pushes, httpd OTA
// handshakes. Everything else (service/RX/heartbeat) belongs to ONE owner
// task, the loop.
class LinkPort {
public:
    // App-level receive callback. PING/PONG are consumed internally; HELLO is
    // handled internally AND forwarded (the app may care about caps).
    using MsgCb = void (*)(uint8_t type, const uint8_t* payload, uint16_t len, void* ctx);

    void begin(ILinkStream& stream, IClock& clock, uint16_t fwVer, uint32_t caps);
    void onMessage(MsgCb cb, void* ctx) { _cb = cb; _ctx = ctx; }

    // Queue one frame. Returns false if it was dropped (queue full of
    // higher-priority traffic). prio defaults per LinkProto::defaultPrio.
    bool send(uint8_t type, const uint8_t* payload, uint16_t len);
    bool send(uint8_t type, const uint8_t* payload, uint16_t len, LinkProto::Prio prio);

    void service();          // call from loop(); never blocks

    bool up() const { return _up; }
    uint16_t peerFwVer() const { return _peerFwVer; }
    uint32_t peerCaps() const { return _peerCaps; }
    uint32_t lastRxMs() const { return _lastRxMs; }

    // Diagnostics
    uint32_t txDropped() const { return _txDropped; }
    uint32_t rxSeqGaps() const { return _rxSeqGaps; }
    const LinkCodec::Decoder& decoder() const { return _dec; }

    static constexpr int QUEUE_SLOTS = 12;

private:
    static void onFrameTrampoline(uint8_t type, uint8_t seq,
                                  const uint8_t* payload, uint16_t len, void* ctx);
    void onFrame(uint8_t type, uint8_t seq, const uint8_t* payload, uint16_t len);
    void sendHello();
    void drainTx();

    // len: 0 = free, SLOT_RESERVED = claimed but still being encoded (skip),
    // else = wire length ready to drain. Publish (len = n) happens after the
    // encode, outside the lock.
    static constexpr uint16_t SLOT_RESERVED = 0xFFFF;
    struct Slot {
        volatile uint16_t len = 0;
        uint8_t  prio = 0;
        uint8_t  bytes[LinkCodec::MAX_WIRE];
    };
    Slot _q[QUEUE_SLOTS];
    LinkLock _lock;              // guards slot claim/free/eviction + seq
    int  _qHead = 0;             // round-robin scan start (order within a prio class)
    int  _curSlot = -1;          // slot being written to the stream (never interleave)
    uint16_t _curOff = 0;

    ILinkStream* _s = nullptr;
    IClock* _clk = nullptr;
    LinkCodec::Decoder _dec;
    MsgCb _cb = nullptr;
    void* _ctx = nullptr;

    uint8_t  _txSeq = 0;
    bool     _haveRxSeq = false;
    uint8_t  _lastRxSeq = 0;
    uint32_t _rxSeqGaps = 0;
    uint32_t _txDropped = 0;

    uint16_t _fwVer = 0;
    uint32_t _caps = 0;
    uint16_t _peerFwVer = 0;
    uint32_t _peerCaps = 0;

    bool     _up = false;
    uint32_t _lastRxMs = 0;
    uint32_t _lastPingMs = 0;
    uint32_t _lastHelloMs = 0;
};
