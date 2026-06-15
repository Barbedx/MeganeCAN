#include "ScreenDecode.h"

namespace ScreenDecode {

void asciiz(const uint8_t* payload, int len, int a, int b, char* dst, int dstSize)
{
    // Collect bytes a..b (inclusive), stop at NUL, keep only printable ASCII (others
    // dropped, matching the proxy's bytesTxt/asciiz). Then trim leading/trailing
    // spaces. dstSize includes room for the terminating NUL.
    char tmp[64];
    int n = 0;
    for (int i = a; i <= b && i < len && n < (int)sizeof(tmp) - 1; i++) {
        uint8_t c = payload[i];
        if (c == 0) break;
        if (c >= 0x20 && c < 0x7f) tmp[n++] = (char)c;
    }
    tmp[n] = '\0';

    // trim leading
    int start = 0;
    while (start < n && tmp[start] == ' ') start++;
    // trim trailing
    int end = n - 1;
    while (end >= start && tmp[end] == ' ') end--;

    int out = 0;
    for (int i = start; i <= end && out < dstSize - 1; i++)
        dst[out++] = tmp[i];
    dst[out] = '\0';
}

void menu(const uint8_t* payload, int len, ScreenModel& m)
{
    if (len < MENU_MIN_LEN) return;

    m.mode   = ScreenModel::MENU;
    m.scroll = payload[OFF_SCROLL];
    m.sel    = -1;                                  // fresh menu clears highlight
    asciiz(payload, len, OFF_HEADER, 36, m.header, sizeof(m.header));
    m.item0Id = payload[OFF_ITEM0_MARK];
    asciiz(payload, len, OFF_ITEM0, 63, m.item0, sizeof(m.item0));
    m.item1Id = payload[OFF_ITEM1_MARK];
    asciiz(payload, len, OFF_ITEM1, 95, m.item1, sizeof(m.item1));
}

void segText(const uint8_t* payload, int len, ScreenModel& m)
{
    if (len < SEG_MIN_LEN) return;
    if (!(payload[0] == 0x10 && payload[1] == 0x19 && payload[2] == 0x76)) return;

    m.mode = ScreenModel::MENU;
    asciiz(payload, len, SEG_NEW, SEG_NEW + 11, m.header, sizeof(m.header));
    asciiz(payload, len, SEG_OLD, SEG_OLD + 7,  m.item0,  sizeof(m.item0));
}

bool frame(const Frame& f, ScreenModel& m)
{
    // Highlight: 07 29 01 <rowId>  (0x7E = item0, 0x7F = item1)
    if (f.len >= 4 && f.data[0] == 0x07 && f.data[1] == 0x29 && f.data[2] == 0x01) {
        m.sel = f.data[3];
        return true;
    }
    return false;
}

} // namespace ScreenDecode
