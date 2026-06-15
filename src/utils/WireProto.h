#pragma once
#include <Arduino.h>

// ============================================================================
// WireProto — the single UART contract between this firmware and PC-side tools
// (serial proxy / CAN display emulator). One source of truth: firmware emits per
// these tags, the PC parses them. Keep tags here and in the proxy parser in sync.
//
// Line format: one message per line. A tagged line starts with '@'; anything
// else is free-text human log (timestamps/diagnostics) the PC shows as-is.
//
//   firmware -> PC
//     @TX <id-hex> <b0> <b1> ...    outbound CAN frame (display <- ESP). The feed
//                                   the PC virtual display decodes (AFFA3 ISO-TP).
//     @RX <id-hex> <b0> ...         inbound CAN frame (ESP <- bus). (future: car capture)
//     @EV <name> <k=v> ...          structured event (e.g. heap, bt, ams). (future)
//
//   PC -> firmware
//     @KEY <code-dec> <hold>        emulate a display key press. (today via HTTP /emulate/key)
//     @INJ <id-hex> <b0> ...        inject a CAN RX frame — used to ACK the display
//                                   handshake so the bench runs without the 2s no-display
//                                   timeout: reply on (sentId | 0x400) with 0x74 (DONE) or
//                                   30 01 00 (PARTIAL). (future: closed-loop emulator)
//
// v1 is compact tags (cheap on the ESP). v2 may wrap as JSON lines if stricter
// typing is wanted; bump a version tag then.
// ============================================================================

namespace WireProto
{
    // Tag strings (kept as constants so the emit/parse sides can't drift).
    constexpr const char *TAG_TX  = "@TX";
    constexpr const char *TAG_RX  = "@RX";
    constexpr const char *TAG_EV  = "@EV";
    constexpr const char *TAG_KEY = "@KEY";
    constexpr const char *TAG_INJ = "@INJ";

    // Emit one outbound CAN frame as "@TX <id> <bytes>". This is the exact format
    // the PC emulator parses; do not change it without updating the proxy parser.
    inline void emitTx(uint32_t id, const uint8_t *data, uint8_t len)
    {
        char buf[64];
        int p = snprintf(buf, sizeof(buf), "%s %03X", TAG_TX, (unsigned)id);
        for (int i = 0; i < len && p < (int)sizeof(buf) - 4; i++)
            p += snprintf(buf + p, sizeof(buf) - p, " %02X", data[i]);
        Serial.println(buf);
    }
}
