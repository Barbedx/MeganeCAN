#pragma once
#include <stdint.h>
#include "ScreenModel.h"
#include "../bus/Frame.h"

// Decode AFFA3 0x151 screen traffic into a ScreenModel. One semantics shared by the
// firmware, the PC proxy and the tests (ported from serial_proxy.py onTx/decodeMenu).
//
// Usage: feed a reassembled showMenu payload to menu(); feed single control frames
// (highlight / info-popup first frames) to frame(). All screens (menu / now-playing
// / notification) are showMenu over 0x151, so menu() covers them.
namespace ScreenDecode {

// Layout of the reassembled showMenu payload (includes the 0x10 0x5A header).
constexpr int MENU_MIN_LEN   = 96;
constexpr int OFF_SCROLL     = 10;
constexpr int OFF_HEADER     = 11;   // .. 36  (26 bytes)
constexpr int OFF_ITEM0_MARK = 38;
constexpr int OFF_ITEM0      = 39;   // .. 63  (25 bytes)
constexpr int OFF_ITEM1_MARK = 65;
constexpr int OFF_ITEM1      = 66;   // .. 95  (30 bytes)

// Decode a reassembled showMenu payload. No-op if len < MENU_MIN_LEN. Sets mode=MENU,
// fills header/items/markers/scroll, and resets sel (a fresh menu clears highlight).
void menu(const uint8_t* payload, int len, ScreenModel& out);

// UpdateList 8-segment setText payload (id 0x121), layout from UpdateListBase::setText:
//   [0]10 [1]19 [2]76 [3]chan [4]loc  [5..12]old(8)  [13]10  [14..25]new(12)
// The "new" field is the text shown after the update -> header; "old" -> item0.
constexpr int SEG_MIN_LEN = 26;
constexpr int SEG_OLD     = 5;    // .. 12 (8 bytes)
constexpr int SEG_NEW     = 14;   // .. 25 (12 bytes)
void segText(const uint8_t* payload, int len, ScreenModel& out);

// Apply a single non-reassembled control frame to the model:
//   07 29 01 <rowId>          -> highlight (sets out.sel)
// Returns true if the frame was recognised + applied.
bool frame(const Frame& f, ScreenModel& out);

// Trim + copy printable ASCII from payload[a..b] (inclusive), stopping at NUL, into
// dst (dstSize includes the NUL). Exposed for tests.
void asciiz(const uint8_t* payload, int len, int a, int b, char* dst, int dstSize);

} // namespace ScreenDecode
