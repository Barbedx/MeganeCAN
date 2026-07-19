#pragma once
#include <stdint.h>
#include "LinkProto.h"

// Stateless encoder + byte-pump decoder for LinkProto frames. Portable; the
// native suite round-trips it and fuzzes the resync paths.
namespace LinkCodec
{
    // Encoded wire size upper bound (COBS overhead + leading/trailing delimiters).
    constexpr uint16_t MAX_WIRE = LinkProto::MAX_FRAME + LinkProto::MAX_FRAME / 254 + 3;

    // Build one wire frame (COBS + trailing 0x00 delimiter) into out.
    // Returns wire length, or 0 if payloadLen exceeds MAX_PAYLOAD.
    uint16_t encode(uint8_t type, uint8_t seq,
                    const uint8_t* payload, uint16_t payloadLen,
                    uint8_t* out /* >= MAX_WIRE bytes */);

    // Push-parser: feed raw UART bytes; fires the callback for every frame that
    // survives COBS + CRC + version checks. Corruption just resyncs on the next
    // 0x00 and bumps a counter — the link never wedges.
    class Decoder {
    public:
        using FrameCb = void (*)(uint8_t type, uint8_t seq,
                                 const uint8_t* payload, uint16_t len, void* ctx);
        void onFrame(FrameCb cb, void* ctx) { _cb = cb; _ctx = ctx; }
        void feed(const uint8_t* data, uint16_t len);
        void feed(uint8_t b);

        uint32_t frames() const   { return _frames; }
        uint32_t badCrc() const   { return _badCrc; }
        uint32_t badCobs() const  { return _badCobs; }
        uint32_t badVer() const   { return _badVer; }
        uint32_t overruns() const { return _overrun; }

    private:
        FrameCb _cb = nullptr;
        void*   _ctx = nullptr;
        uint8_t _buf[MAX_WIRE];
        uint16_t _len = 0;
        bool _skip = false;      // oversized frame: discard until next delimiter
        uint32_t _frames = 0, _badCrc = 0, _badCobs = 0, _badVer = 0, _overrun = 0;
    };
}
