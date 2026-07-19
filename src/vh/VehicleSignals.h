#pragma once
#include <stdint.h>

// The VH board's decoded signal model (ARCHITECTURE-V2 §5). All values are
// scaled integers; each signal carries the clock ms of its last update so
// consumers can tell fresh data from stale. Portable.
namespace Vh
{
    enum Sig : uint8_t {
        SIG_SPEED = 0,     // km/h x100
        SIG_RPM,           // rpm
        SIG_STEERING,      // degrees x10, signed (- = left)
        SIG_OUT_TEMP,      // °C
        SIG_ENG_TEMP,      // °C
        SIG_ODOMETER,      // km
        SIG_DOOR_FL,       // 0/1
        SIG_DOOR_FR,
        SIG_DOOR_RL,
        SIG_DOOR_RR,
        SIG_BOOT,
        SIG_LIGHT_POS,     // position/side lights
        SIG_LIGHT_DIPPED,  // low beam
        SIG_LIGHT_MAIN,    // high beam
        SIG_IND_LEFT,      // turn indicator (latched over blink pulses)
        SIG_IND_RIGHT,
        SIG_FOG_FRONT,
        SIG_FOG_REAR,
        SIG_REVERSE,
        SIG_HANDBRAKE,
        SIG_BRAKE,         // brake pedal
        SIG_KEY_ON,
        SIG_KEY_ACC,
        SIG_BACKLIGHT,     // instrument illumination 0x00-0xFE (0xFF = off)
        SIG_COUNT
    };

    // Short name for logs/JSON. Inline so MM (which doesn't compile src/vh
    // .cpp files) can render VH signal names too.
    inline const char* sigName(uint8_t s)
    {
        static const char* const names[SIG_COUNT] = {
            "speed", "rpm", "steering", "out_temp", "eng_temp", "odometer",
            "door_fl", "door_fr", "door_rl", "door_rr", "boot",
            "light_pos", "light_dip", "light_main", "ind_left", "ind_right",
            "fog_front", "fog_rear", "reverse", "handbrake", "brake",
            "key_on", "key_acc", "backlight",
        };
        return s < SIG_COUNT ? names[s] : "?";
    }

    struct VehicleState {
        int32_t  val[SIG_COUNT] = {};
        uint32_t ts[SIG_COUNT]  = {};   // 0 = never seen

        bool has(uint8_t s) const { return s < SIG_COUNT && ts[s] != 0; }
        // Fresh = updated within maxAgeMs (guards the canbox against stale data).
        bool fresh(uint8_t s, uint32_t nowMs, uint32_t maxAgeMs) const {
            return has(s) && (nowMs - ts[s]) <= maxAgeMs;
        }
        int32_t get(uint8_t s, int32_t fallback = 0) const {
            return has(s) ? val[s] : fallback;
        }
        void set(uint8_t s, int32_t v, uint32_t nowMs) {
            if (s >= SIG_COUNT) return;
            val[s] = v;
            ts[s] = nowMs ? nowMs : 1;   // 0 is reserved for "never"
        }
    };
}
