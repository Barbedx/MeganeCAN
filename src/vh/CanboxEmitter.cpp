#include "CanboxEmitter.h"

namespace Vh
{
    uint8_t CanboxEmitter::checksum(uint8_t cmd, uint8_t len, const uint8_t* payload)
    {
        uint32_t sum = cmd + len;
        for (uint8_t i = 0; i < len; i++)
            sum += payload[i];
        return (uint8_t)(sum & 0xFF) ^ 0xFF;
    }

    uint8_t CanboxEmitter::buildFrame(uint8_t cmd, const uint8_t* payload, uint8_t len,
                                      uint8_t* out)
    {
        uint8_t n = 0;
        out[n++] = 0x2E;
        out[n++] = cmd;
        out[n++] = len;
        for (uint8_t i = 0; i < len; i++)
            out[n++] = payload[i];
        out[n++] = checksum(cmd, len, payload);
        return n;
    }

    void CanboxEmitter::emit(uint8_t cmd, const uint8_t* payload, uint8_t len)
    {
        if (!_enabled || !_write) return;
        uint8_t frame[40];
        uint8_t n = buildFrame(cmd, payload, len, frame);
        _write(frame, n, _wctx);   // one call = one whole frame, never interleaved
        _framesSent++;
    }

    void CanboxEmitter::sendKey(uint8_t code, uint8_t state)
    {
        const uint8_t p[2] = { code, state };
        emit(0x20, p, 2);
    }

    uint8_t CanboxEmitter::doorsMask(const VehicleState& s)
    {
        uint8_t m = 0;
        if (s.get(SIG_DOOR_FL)) m |= 0x80;   // driver (LHD)
        if (s.get(SIG_DOOR_FR)) m |= 0x40;   // passenger
        if (s.get(SIG_DOOR_RR)) m |= 0x20;
        if (s.get(SIG_DOOR_RL)) m |= 0x10;
        if (s.get(SIG_BOOT))    m |= 0x08;
        return m;
    }

    uint8_t CanboxEmitter::lightsMask(const VehicleState& s)
    {
        uint8_t m = 0;
        if (s.get(SIG_LIGHT_POS))    m |= 0x80;
        if (s.get(SIG_LIGHT_DIPPED)) m |= 0x40;
        if (s.get(SIG_LIGHT_MAIN))   m |= 0x20;
        if (s.get(SIG_IND_LEFT))     m |= 0x10;
        if (s.get(SIG_IND_RIGHT))    m |= 0x08;
        return m;
    }

    bool CanboxEmitter::due(Sched& sc, uint32_t nowMs, uint32_t interval,
                            uint32_t value, bool onChange)
    {
        bool timeUp  = (nowMs - sc.lastMs) >= interval || sc.lastMs == 0;
        bool changed = onChange && value != sc.lastVal && sc.lastVal != 0xFFFFFFFF;
        if (!timeUp && !changed) return false;
        sc.lastMs = nowMs ? nowMs : 1;   // 0 stays the "never fired" sentinel
        sc.lastVal = value;
        return true;
    }

    void CanboxEmitter::tick(const VehicleState& s, uint32_t nowMs)
    {
        if (!_enabled) return;

        // 0x24 doors: 250ms + on-change
        if (s.has(SIG_DOOR_FL) || s.has(SIG_DOOR_FR) || s.has(SIG_DOOR_RL) ||
            s.has(SIG_DOOR_RR) || s.has(SIG_BOOT))
        {
            uint8_t m = doorsMask(s);
            if (due(_doors, nowMs, 250, m, true))
                emit(0x24, &m, 1);
        }

        // 0x28 outside temp: 5s; 12B zeros except [5]=(t+40)*2
        if (s.has(SIG_OUT_TEMP) && due(_temp, nowMs, 5000, 0, false))
        {
            uint8_t p[12] = {};
            int32_t t = s.get(SIG_OUT_TEMP);
            if (t < -40) t = -40;
            if (t > 87)  t = 87;
            p[5] = (uint8_t)((t + 40) * 2);
            emit(0x28, p, sizeof(p));
        }

        // 0x29 steering: 200ms; int16 LE, -540..+540 (state is deg x10)
        if (s.has(SIG_STEERING) && due(_steer, nowMs, 200, 0, false))
        {
            int32_t deg = s.get(SIG_STEERING) / 10;
            if (deg < -540) deg = -540;
            if (deg >  540) deg =  540;
            uint8_t p[2] = { (uint8_t)(deg & 0xFF), (uint8_t)((deg >> 8) & 0xFF) };
            emit(0x29, p, 2);
        }

        // 0x7D 01 lights: 200ms + on-change
        if (s.has(SIG_LIGHT_POS) || s.has(SIG_LIGHT_DIPPED) || s.has(SIG_LIGHT_MAIN) ||
            s.has(SIG_IND_LEFT) || s.has(SIG_IND_RIGHT))
        {
            uint8_t m = lightsMask(s);
            if (due(_lights, nowMs, 200, m, true))
            {
                uint8_t p[2] = { 0x01, m };
                emit(0x7D, p, 2);
            }
        }

        // 0x7D 03 speed: 500ms; [spd*100 LE:2][0][0]
        if (s.has(SIG_SPEED) && due(_speed, nowMs, 500, 0, false))
        {
            uint32_t v = (uint32_t)s.get(SIG_SPEED);   // already km/h x100
            if (v > 0xFFFF) v = 0xFFFF;
            uint8_t p[5] = { 0x03, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8), 0, 0 };
            emit(0x7D, p, sizeof(p));
        }

        // 0x7D 04 odometer: 10s; [odo LE:3][0xF2][0x08][trip1:3][trip2:3]
        if (s.has(SIG_ODOMETER) && due(_odo, nowMs, 10000, 0, false))
        {
            uint32_t km = (uint32_t)s.get(SIG_ODOMETER);
            uint8_t p[12] = { 0x04,
                              (uint8_t)(km & 0xFF), (uint8_t)((km >> 8) & 0xFF),
                              (uint8_t)((km >> 16) & 0xFF),
                              0xF2, 0x08,   // magic cargo-culted from captures (§8.4)
                              0, 0, 0, 0, 0, 0 };
            emit(0x7D, p, sizeof(p));
        }

        // 0x7D 0A RPM: 333ms; uint16 LE (scaling configurable — §8.4 verify)
        if (s.has(SIG_RPM) && due(_rpm, nowMs, 333, 0, false))
        {
            int32_t r = (int32_t)((int64_t)s.get(SIG_RPM) * _rpmMul / _rpmDiv);
            if (r < 0) r = 0;
            if (r > 0xFFFF) r = 0xFFFF;
            uint8_t p[3] = { 0x0A, (uint8_t)(r & 0xFF), (uint8_t)(r >> 8) };
            emit(0x7D, p, sizeof(p));
        }
    }
}
