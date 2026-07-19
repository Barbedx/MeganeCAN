#include "VehicleDecoder.h"
#include "VehicleDbc.h"

namespace Vh
{
    const char* sigName(uint8_t s)
    {
        static const char* names[SIG_COUNT] = {
            "speed", "rpm", "steering", "out_temp", "eng_temp", "odometer",
            "door_fl", "door_fr", "door_rl", "door_rr", "boot",
            "light_pos", "light_dip", "light_main", "ind_left", "ind_right",
            "fog_front", "fog_rear", "reverse", "handbrake", "brake",
            "key_on", "key_acc", "backlight",
        };
        return s < SIG_COUNT ? names[s] : "?";
    }

    void VehicleDecoder::setSignalEnabled(uint8_t sig, bool on)
    {
        if (sig >= SIG_COUNT) return;
        if (on) _sigDisabled &= ~(1u << sig);
        else    _sigDisabled |= (1u << sig);
    }

    bool VehicleDecoder::signalEnabled(uint8_t sig) const
    {
        return sig < SIG_COUNT && !(_sigDisabled & (1u << sig));
    }

    void VehicleDecoder::applyField(const FieldDef& fd, const Frame& f, uint32_t nowMs)
    {
        if (!signalEnabled(fd.sig)) return;
        if ((uint8_t)(fd.startByte + fd.numBytes) > f.len) return;

        uint32_t raw = 0;
        for (uint8_t i = 0; i < fd.numBytes; i++)
            raw = (raw << 8) | f.data[fd.startByte + i];

        raw >>= fd.shift;
        if (fd.bits < 32)
            raw &= (uint32_t)((1ull << fd.bits) - 1);

        int32_t v = (int32_t)((int64_t)raw * fd.mul / fd.div) + fd.add;

        if (fd.latchMs)
        {
            // Timestamp latch (§8.5): a 1 re-arms; a 0 only lands after expiry.
            if (v)
                _state.set(fd.sig, 1, nowMs);
            else if (_state.has(fd.sig) && _state.val[fd.sig] &&
                     (nowMs - _state.ts[fd.sig]) <= fd.latchMs)
                return;   // still inside the blink window — hold the 1
            else
                _state.set(fd.sig, 0, nowMs);
            return;
        }

        _state.set(fd.sig, v, nowMs);
    }

    void VehicleDecoder::feed(const Frame& f, uint32_t nowMs)
    {
        for (uint8_t i = 0; i < N_FRAMES; i++)
        {
            const FrameDef& fr = FRAMES[i];
            if (fr.id != f.id) continue;
            if (f.len < fr.minDlc) return;
            for (uint8_t j = 0; j < fr.nFields; j++)
                applyField(fr.fields[j], f, nowMs);
            _known++;
            return;
        }
        _unknown++;
        noteUnknown((uint16_t)f.id);
    }

    void VehicleDecoder::tick(uint32_t nowMs)
    {
        // Expire indicator latches when the pulses stop entirely (no 0-frames).
        for (uint8_t i = 0; i < N_FRAMES; i++)
            for (uint8_t j = 0; j < FRAMES[i].nFields; j++)
            {
                const FieldDef& fd = FRAMES[i].fields[j];
                if (!fd.latchMs) continue;
                if (_state.has(fd.sig) && _state.val[fd.sig] &&
                    (nowMs - _state.ts[fd.sig]) > fd.latchMs)
                    _state.set(fd.sig, 0, nowMs);
            }
    }

    void VehicleDecoder::noteUnknown(uint16_t id)
    {
        for (uint8_t i = 0; i < _censusN; i++)
            if (_census[i].id == id) { _census[i].count++; return; }
        if (_censusN < CENSUS_SLOTS)
        {
            _census[_censusN].id = id;
            _census[_censusN].count = 1;
            _censusN++;
        }
    }
}
