#include "DispCmdServer.h"
#include "../link/LinkProto.h"
#include <string.h>

using namespace LinkProto;

namespace DispCmd
{
    // Strings are NUL-terminated, packed back to back; unterminated tails
    // yield empty strings (the working copy below guarantees a final NUL).
    static const char* takeStr(const uint8_t*& p, const uint8_t* end)
    {
        const char* s = (const char*)p;
        while (p < end && *p) p++;
        if (p < end) p++;            // consume the NUL
        else return "";              // unterminated -> treat as empty
        return s;
    }

    void execute(IDisplay& d, const uint8_t* payload, uint16_t len)
    {
        if (len < 1) return;
        uint8_t op = payload[0];

        // Copy so takeStr can rely on in-buffer NULs even for the final string.
        static uint8_t buf[MAX_PAYLOAD + 1];
        uint16_t n = (uint16_t)(len - 1);
        if (n > MAX_PAYLOAD) n = MAX_PAYLOAD;
        memcpy(buf, payload + 1, n);
        buf[n] = 0;
        const uint8_t* q = buf;
        const uint8_t* end = buf + n;

        switch (op)
        {
        case DO_SET_TEXT:
        {
            if (n < 1) return;
            uint8_t digit = *q++;
            d.setText((const char*)q, digit);
            return;
        }
        case DO_SET_STATE:
            if (n >= 1) d.setState(buf[0] != 0);
            return;
        case DO_SET_TIME:
            d.setTime((const char*)buf);
            return;
        case DO_SHOW_MENU:
        {
            if (n < 1) return;
            uint8_t scroll = *q++;
            const char* h  = takeStr(q, end);
            const char* i1 = takeStr(q, end);
            const char* i2 = takeStr(q, end);
            d.showMenu(h, i1, i2, scroll);
            return;
        }
        case DO_INFO_POPUP:
        {
            const char* l1 = takeStr(q, end);
            const char* l2 = takeStr(q, end);
            const char* l3 = takeStr(q, end);
            d.showInfoPopup(l1, l2, l3);
            return;
        }
        case DO_HIDE_INFO:  d.hideInfoPopup();  return;
        case DO_CONFIRM:
        {
            const char* c  = takeStr(q, end);
            const char* r1 = takeStr(q, end);
            const char* r2 = takeStr(q, end);
            d.showConfirmBox(c, r1, r2);
            return;
        }
        case DO_FULLSCREEN:
        {
            const char* l1 = takeStr(q, end);
            const char* l2 = takeStr(q, end);
            const char* l3 = takeStr(q, end);
            d.showFullscreenText(l1, l2, l3);
            return;
        }
        case DO_HIDE_FULL:  d.hideFullscreenText(); return;
        case DO_POPUP_TEXT:
            if (n >= 3)
                d.showPopupText((const char*)(buf + 3), buf[0], buf[1], buf[2]);
            return;
        case DO_HIDE_POPUP: d.hidePopup(); return;
        case DO_KEY:
            if (n >= 3)
                d.ProcessKey(
                    (AffaCommon::AffaKey)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8)),
                    buf[2] != 0);
            return;
        case DO_AUX:
            if (n >= 1) d.setAuxMode(buf[0] != 0);
            return;
        default:
            return;   // forward-compatible: unknown ops are skipped
        }
    }
}
