#pragma once
#include <stdint.h>

// The decoded state of an AFFA3 display screen — the one shared "what's on the
// screen" model produced by ScreenDecode and exposed by every IVirtualDisplay.
// Firmware, the PC proxy, and the native tests all agree on these semantics
// (ported from tools/serial_proxy.py's decodeMenu/renderInfo).
//
// Field offsets refer to the reassembled showMenu ISO-TP payload (id 0x151), which
// begins with the [0x10][0x5A] first-frame header:
//   [10]      scrollLock   (0x0B down / 0x07 up / 0x0C both / 0x00 none)
//   [11..36]  header text  (26 bytes)
//   [38]      item0 marker (0x7E)   [39..63] item0 text (25 bytes)
//   [65]      item1 marker (0x7F)   [66..95] item1 text (30 bytes)
struct ScreenModel {
    enum Mode : uint8_t { NONE, MENU, INFO };

    Mode    mode    = NONE;
    char    header[27] = {0};       // [11..36]
    char    item0[26]  = {0};       // [39..63]
    char    item1[31]  = {0};       // [66..95]
    uint8_t item0Id = 0;            // marker [38] (0x7E)
    uint8_t item1Id = 0;            // marker [65] (0x7F)
    int16_t sel     = -1;           // highlighted rowId (0x7E/0x7F); -1 = none
    uint8_t scroll  = 0;            // [10] scrollLock

    char    info[3][9] = {{0}};     // info popup: 3 slots x 8 chars (+NUL)
    uint8_t infoCount  = 0;

    void clear() { *this = ScreenModel(); }
};
