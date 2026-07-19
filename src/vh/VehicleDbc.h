#pragma once
#include <stdint.h>
#include "VehicleSignals.h"

// Mégane II main-CAN decode table — the canbox-nissan FrameConfig/FieldConfig
// pattern (ARCHITECTURE-V2 §8.5): id -> byte extract -> formula -> named slot.
//
// !! HYPOTHESIS TABLE (§8.1): IDs and layouts come from the Clio III decode
// (x0r.fr/blog/39, same platform era). The P2 correlation sniff on the real car
// confirms or corrects them; per-signal disable + id remap live in VhConfig so
// corrections need no rebuild. VH is listen-only throughout.
//
// Extraction: raw = big-endian(numBytes at startByte); field = (raw >> shift)
// & ((1<<bits)-1); value = field * mul / div + add. latchMs > 0 = a 1-value
// re-arms a timestamp latch and 0 only clears after it expires (turn-indicator
// blink pulses -> steady state, §8.5).
namespace Vh
{
    struct FieldDef {
        uint8_t  sig;        // Sig
        uint8_t  startByte;
        uint8_t  numBytes;   // 1..4, big-endian
        uint8_t  shift;      // right-shift of the BE-extracted raw
        uint8_t  bits;       // width after shift (32 = all)
        int32_t  mul, div, add;
        uint16_t latchMs;    // 0 = none
    };

    struct FrameDef {
        uint16_t id;
        uint8_t  minDlc;     // ignore shorter frames
        const FieldDef* fields;
        uint8_t  nFields;
    };

    // 0x0C2: steering angle b0-1 (raw - 0x8000, 0.1°/LSB) -> deg x10
    constexpr FieldDef F_0C2[] = {
        { SIG_STEERING, 0, 2, 0, 32, 1, 1, -0x8000, 0 },
    };
    // 0x181: RPM b0-1 /8; b5 bit0 brake pedal
    constexpr FieldDef F_181[] = {
        { SIG_RPM,   0, 2, 0, 32, 1, 8, 0, 0 },
        { SIG_BRAKE, 5, 1, 0, 1,  1, 1, 0, 0 },
    };
    // 0x354: speed b0-1 x0.01 km/h (raw is already km/h x100); b4 bit4 brake
    constexpr FieldDef F_354[] = {
        { SIG_SPEED, 0, 2, 0, 32, 1, 1, 0, 0 },
    };
    // 0x551: b0 engine temp -40
    constexpr FieldDef F_551[] = {
        { SIG_ENG_TEMP, 0, 1, 0, 32, 1, 1, -40, 0 },
    };
    // 0x5C5: b0 bit3 handbrake (x0r; mikescotland used bit2 — P2 verifies)
    constexpr FieldDef F_5C5[] = {
        { SIG_HANDBRAKE, 0, 1, 3, 1, 1, 1, 0, 0 },
    };
    // 0x5FD: b0-2 odometer (km)
    constexpr FieldDef F_5FD[] = {
        { SIG_ODOMETER, 0, 3, 0, 32, 1, 1, 0, 0 },
    };
    // 0x60D: b0-2 status bits (bit numbers within the 24-bit BE word);
    // b4 outside temp -40; b5 engine temp -40; b6 bit4 reverse
    constexpr FieldDef F_60D[] = {
        { SIG_BOOT,         0, 3, 23, 1, 1, 1, 0, 0 },
        { SIG_DOOR_RR,      0, 3, 22, 1, 1, 1, 0, 0 },
        { SIG_DOOR_RL,      0, 3, 21, 1, 1, 1, 0, 0 },
        { SIG_DOOR_FR,      0, 3, 20, 1, 1, 1, 0, 0 },
        { SIG_DOOR_FL,      0, 3, 19, 1, 1, 1, 0, 0 },
        { SIG_LIGHT_POS,    0, 3, 18, 1, 1, 1, 0, 0 },
        { SIG_LIGHT_DIPPED, 0, 3, 17, 1, 1, 1, 0, 0 },
        { SIG_IND_RIGHT,    0, 3, 14, 1, 1, 1, 0, 500 },
        { SIG_IND_LEFT,     0, 3, 13, 1, 1, 1, 0, 500 },
        { SIG_LIGHT_MAIN,   0, 3, 11, 1, 1, 1, 0, 0 },
        { SIG_KEY_ON,       0, 3, 10, 1, 1, 1, 0, 0 },
        { SIG_KEY_ACC,      0, 3,  9, 1, 1, 1, 0, 0 },
        { SIG_FOG_FRONT,    0, 3,  8, 1, 1, 1, 0, 0 },
        { SIG_FOG_REAR,     0, 3,  2, 1, 1, 1, 0, 0 },
        { SIG_OUT_TEMP,     4, 1, 0, 32, 1, 1, -40, 0 },
        { SIG_REVERSE,      6, 1, 4, 1,  1, 1, 0, 0 },
    };
    // 0x645: b1 instrument backlight (0xFF = off -> pass raw through)
    constexpr FieldDef F_645[] = {
        { SIG_BACKLIGHT, 1, 1, 0, 32, 1, 1, 0, 0 },
    };

    constexpr FrameDef FRAMES[] = {
        { 0x0C2, 2, F_0C2, 1 },
        { 0x181, 6, F_181, 2 },
        { 0x354, 2, F_354, 1 },
        { 0x551, 1, F_551, 1 },
        { 0x5C5, 1, F_5C5, 1 },
        { 0x5FD, 3, F_5FD, 1 },
        { 0x60D, 7, F_60D, 16 },
        { 0x645, 2, F_645, 1 },
    };
    constexpr uint8_t N_FRAMES = sizeof(FRAMES) / sizeof(FRAMES[0]);
}
