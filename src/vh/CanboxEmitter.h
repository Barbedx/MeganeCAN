#pragma once
#include <stdint.h>
#include "VehicleSignals.h"

// Raise-protocol canbox emitter, Toyota RAV4 2018-2020 message set
// (ARCHITECTURE-V2 §5/§8.4). Framing: [0x2E][cmd][len][payload][cs],
// cs = (cmd + len + Σpayload) XOR 0xFF; UART 38400 8N1, fire-and-forget.
// Per-message schedulers: interval + on-change; keys are sent immediately and
// whole-frame (never interleaved — the write callback emits one full frame).
// Portable: bytes leave via the WriteFn seam, time comes in as nowMs.
namespace Vh
{
    class CanboxEmitter {
    public:
        using WriteFn = void (*)(const uint8_t* frame, uint8_t len, void* ctx);

        void begin(WriteFn write, void* ctx) { _write = write; _wctx = ctx; }
        void setEnabled(bool on) { _enabled = on; }
        bool enabled() const { return _enabled; }

        // DUDU quirk knob (§8.4): RPM scaling differs between references
        // (canbox-nissan rpm*4 vs mikescotland rpm/2) — config, verify in P3.
        void setRpmScale(int16_t mul, int16_t div) { _rpmMul = mul; _rpmDiv = div ? div : 1; }

        // SWC key, RAV4 codes (0x01 VOL+, 0x02 VOL-, 0x07 SOURCE, 0x08 VOICE,
        // 0x16 OK, 0x85 NEXT, 0x86 PREV, 0x88 TEL); state 0=rel/1=press/2=long.
        // Emitted immediately, pre-empting the schedulers.
        void sendKey(uint8_t code, uint8_t state);

        // Run the schedulers against the current vehicle state.
        void tick(const VehicleState& s, uint32_t nowMs);

        // Frame builder + checksum, public for golden-vector tests.
        // out must hold len+4 bytes; returns total frame length.
        static uint8_t buildFrame(uint8_t cmd, const uint8_t* payload, uint8_t len,
                                  uint8_t* out);
        static uint8_t checksum(uint8_t cmd, uint8_t len, const uint8_t* payload);

        // Mégane SWC AffaKey (u16) -> Raise RAV4 key code; 0 = unmapped.
        // Lives with the profile (the HU owns its codes); P3 tunes it on DUDU.
        static uint8_t rav4CodeForAffa(uint16_t affaKey);

        // Raise doors mask (0x24): driver 0x80, passenger 0x40, RR 0x20,
        // RL 0x10, boot 0x08.
        static uint8_t doorsMask(const VehicleState& s);
        // Raise lights mask (0x7D 01), canbox-nissan variant: parking 0x80,
        // low 0x40, high 0x20, left-ind 0x10, right-ind 0x08. (§8.4: verify.)
        static uint8_t lightsMask(const VehicleState& s);

        uint32_t framesSent() const { return _framesSent; }

    private:
        void emit(uint8_t cmd, const uint8_t* payload, uint8_t len);

        WriteFn _write = nullptr;
        void* _wctx = nullptr;
        bool _enabled = true;
        int16_t _rpmMul = 1, _rpmDiv = 1;
        uint32_t _framesSent = 0;

        // Scheduler state per periodic message.
        struct Sched { uint32_t lastMs = 0; uint32_t lastVal = 0xFFFFFFFF; };
        Sched _doors, _temp, _steer, _lights, _speed, _odo, _rpm;

        bool due(Sched& s, uint32_t nowMs, uint32_t interval,
                 uint32_t value, bool onChange);
    };
}
