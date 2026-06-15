#include "UpdateListSegVirtualDisplay.h"
#include "../affa/ScreenDecode.h"

void UpdateListSegVirtualDisplay::decode(const Frame& f)
{
    // Only the text channel (0x121) carries a screen payload; display-ctrl (0x1B1)
    // is enable/disable and just gets ACKed by the base.
    if (f.id != UpdateList::PACKET_ID_SETTEXT) return;

    if (_asm.onFrame(f) && _asm.len() >= ScreenDecode::SEG_MIN_LEN)
        ScreenDecode::segText(_asm.buffer(), _asm.len(), _screen);
}
