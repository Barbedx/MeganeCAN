#include "CarminatVirtualDisplay.h"
#include "../affa/ScreenDecode.h"

void CarminatVirtualDisplay::decode(const Frame& f)
{
    // Highlight is a standalone single frame (07 29 01 <rowId>), not ISO-TP.
    if (f.len >= 1 && f.data[0] == 0x07) {
        ScreenDecode::frame(f, _screen);
        return;
    }

    // showMenu first frame is 10 5A …; continuations are 2N …. Reassemble and decode
    // once enough bytes are present (the proxy decodes progressively the same way).
    // (Info-popup first frames 10 0B 76 … are a different layout — ignored here.)
    if (f.data[0] == 0x10 && f.len >= 2 && f.data[1] != 0x5A)
        return;

    if (_asm.onFrame(f) && _asm.len() >= ScreenDecode::MENU_MIN_LEN)
        ScreenDecode::menu(_asm.buffer(), _asm.len(), _screen);
}
