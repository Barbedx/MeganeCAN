#pragma once
#include <stdint.h>

// HU -> canbox receive parser (ARCHITECTURE-V2 §5/§8.4). Same Raise framing as
// the emitter. Documented commands (0x81 start/stop, 0x90 info request, 0xA6
// time, 0xC0 source, 0xC2 tuner freq, 0xC4 volume, ...) fire the frame
// callback; solitary 0xFF/0xF0 bytes are the spec ACK/NACK (counted, consumed).
// EVERY valid frame also reaches the callback whether documented or not — the
// capture path (unknown frames logged raw to FS) is how the DUDU media-metadata
// message gets reverse-engineered (P4). Portable byte pump.
namespace Vh
{
    class HuRxParser {
    public:
        // known = the cmd is in the documented set; unknown frames are the
        // interesting ones for the RE capture.
        using FrameCb = void (*)(uint8_t cmd, const uint8_t* payload, uint8_t len,
                                 bool known, void* ctx);

        void onFrame(FrameCb cb, void* ctx) { _cb = cb; _ctx = ctx; }
        void feed(const uint8_t* data, uint16_t len);
        void feed(uint8_t b);

        static bool isKnownCmd(uint8_t cmd);

        uint32_t frames() const   { return _frames; }
        uint32_t unknown() const  { return _unknown; }
        uint32_t badCs() const    { return _badCs; }
        uint32_t acks() const     { return _acks; }
        uint32_t nacks() const    { return _nacks; }
        uint32_t noise() const    { return _noise; }

        static constexpr uint8_t MAX_PAYLOAD = 48;

    private:
        enum State : uint8_t { WAIT, CMD, LEN, PAYLOAD, CS };
        State _st = WAIT;
        uint8_t _cmd = 0, _len = 0, _got = 0;
        uint8_t _buf[MAX_PAYLOAD];
        FrameCb _cb = nullptr;
        void* _ctx = nullptr;
        uint32_t _frames = 0, _unknown = 0, _badCs = 0, _acks = 0, _nacks = 0, _noise = 0;
    };
}
