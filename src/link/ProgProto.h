#pragma once
#include <stdint.h>

// Programming transport (CR-01) — firmware upload / recovery / flashing.
// Independent of the runtime LinkProto: it shares the COBS+CRC16 byte transport
// (LinkPort) but owns its own type range (0x70-0x7F) and its own version, and
// is deliberately FROZEN — the runtime protocol may evolve freely without ever
// touching the machinery that reflashes boards, and vice versa.
namespace ProgProto
{
    constexpr uint8_t VERSION = 1;

    enum Type : uint8_t {
        OTA_BEGIN = 0x70, // {size:4 LE}
        OTA_DATA  = 0x71, // {offset:4 LE, data...} (stop-and-wait)
        OTA_END   = 0x72, // {} -> flash verify + boot-slot switch + reboot
        OTA_STAT  = 0x73, // {code:1 (0=ok/ack), detail:4 LE (next offset / error)}
    };

    inline bool isProgType(uint8_t t) { return t >= 0x70 && t <= 0x7F; }
}
