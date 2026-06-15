#include "UpdateListLcdVirtualDisplay.h"
#include "../affa/ScreenDecode.h"

void UpdateListLcdVirtualDisplay::decode(const Frame& f)
{
    if (f.id != UpdateList::PACKET_ID_SETTEXT) return;   // text channel only

    // Highlight (if the mono UL panel uses the same 07 29 01 convention).
    if (f.len >= 1 && f.data[0] == 0x07) {
        ScreenDecode::frame(f, _screen);
        return;
    }

    // PROVISIONAL showMenu decode — see header note. Same reassembly as Carminat.
    if (_asm.onFrame(f) && _asm.len() >= ScreenDecode::MENU_MIN_LEN)
        ScreenDecode::menu(_asm.buffer(), _asm.len(), _screen);
}
