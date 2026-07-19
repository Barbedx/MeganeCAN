#include "DispLink.h"
#include "../link/LinkPort.h"
#include "../link/LinkProto.h"
#include "../link/ProgProto.h"
#include "../link/HwSerialLinkStream.h"
#include "../bus/ArduinoClock.h"
#include "../utils/Log.h"
#include <Arduino.h>
#include <sys/time.h>
#include <string.h>

using namespace LinkProto;

namespace DispLink
{
    static constexpr uint16_t DISP_FW_VER = 0x0200;   // 2.0 — the thin peripheral
    static constexpr uint32_t DISP_CAPS =
        Caps::CAP_DISPLAY | Caps::CAP_CAN | Caps::CAP_KEYBOARD | Caps::CAP_OTA;
    static constexpr int PIN_TX = 21, PIN_RX = 20;    // §3.2 UART1 -> GW

    static LinkPort s_port;
    static HwSerialLinkStream s_stream;
    static AffaDisplayBase* s_display = nullptr;
    static MediaRouter* s_router = nullptr;
    static HuLinkMediaSource s_huMedia;
    static LinkTunnel* s_tunnel = nullptr;
    // Never begin()ed: the C3 keeps no filesystem — the tunnel's FILE/CAP ops
    // answer "nothing here" gracefully while OTA (the part that matters) works.
    static VhFs::FsLogger s_noFs;
    static bool s_up = false;

    HuLinkMediaSource& mediaSource() { return s_huMedia; }
    bool up() { return s_up && s_port.up(); }

    // ---- DISP_CMD -> local driver ------------------------------------------
    // Strings are NUL-terminated, packed back to back; a short/malformed frame
    // just yields empty strings (never reads past len).
    static const char* takeStr(const uint8_t*& p, const uint8_t* end)
    {
        const char* s = (const char*)p;
        while (p < end && *p) p++;
        if (p < end) p++;            // consume the NUL
        else return "";              // unterminated -> treat as empty
        return s;
    }

    static void handleDispCmd(const uint8_t* p, uint16_t len)
    {
        if (!s_display || len < 1) return;
        uint8_t op = p[0];
        // Copy so takeStr can rely on in-buffer NULs even for the final string.
        static uint8_t buf[MAX_PAYLOAD + 1];
        uint16_t n = len - 1;
        memcpy(buf, p + 1, n);
        buf[n] = 0;
        const uint8_t* q = buf;
        const uint8_t* end = buf + n;

        switch (op)
        {
        case DO_SET_TEXT:
        {
            if (n < 1) return;
            uint8_t digit = *q++;
            s_display->setText((const char*)q, digit);
            return;
        }
        case DO_SET_STATE:
            if (n >= 1) s_display->setState(buf[0] != 0);
            return;
        case DO_SET_TIME:
            s_display->setTime((const char*)buf);
            return;
        case DO_SHOW_MENU:
        {
            if (n < 1) return;
            uint8_t scroll = *q++;
            const char* h  = takeStr(q, end);
            const char* i1 = takeStr(q, end);
            const char* i2 = takeStr(q, end);
            s_display->showMenu(h, i1, i2, scroll);
            return;
        }
        case DO_INFO_POPUP:
        {
            const char* l1 = takeStr(q, end);
            const char* l2 = takeStr(q, end);
            const char* l3 = takeStr(q, end);
            s_display->showInfoPopup(l1, l2, l3);
            return;
        }
        case DO_HIDE_INFO:  s_display->hideInfoPopup();  return;
        case DO_CONFIRM:
        {
            const char* c  = takeStr(q, end);
            const char* r1 = takeStr(q, end);
            const char* r2 = takeStr(q, end);
            s_display->showConfirmBox(c, r1, r2);
            return;
        }
        case DO_FULLSCREEN:
        {
            const char* l1 = takeStr(q, end);
            const char* l2 = takeStr(q, end);
            const char* l3 = takeStr(q, end);
            s_display->showFullscreenText(l1, l2, l3);
            return;
        }
        case DO_HIDE_FULL:  s_display->hideFullscreenText(); return;
        case DO_POPUP_TEXT:
            if (n >= 3)
                s_display->showPopupText((const char*)(buf + 3), buf[0], buf[1], buf[2]);
            return;
        case DO_HIDE_POPUP: s_display->hidePopup(); return;
        case DO_KEY:
            if (n >= 3)
                s_display->ProcessKey(
                    (AffaCommon::AffaKey)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8)),
                    buf[2] != 0);
            return;
        case DO_AUX:
            if (n >= 1) s_display->setAuxMode(buf[0] != 0);
            return;
        default:
            return;   // forward-compatible: unknown ops are skipped
        }
    }

    // ---- link RX ------------------------------------------------------------
    static void onMsg(uint8_t type, const uint8_t* p, uint16_t len, void*)
    {
        // ProgProto replies (OTA_STAT from GW) belong to the PC tool on USB —
        // our own tunnel never emits/needs STAT inbound.
        if (type == ProgProto::OTA_STAT)
        {
            Serial.print("@PROG ");
            char b[3];
            snprintf(b, sizeof(b), "%02X", type);
            Serial.print(b);
            for (uint16_t i = 0; i < len; i++)
            {
                snprintf(b, sizeof(b), "%02X", p[i]);
                Serial.print(b);
            }
            Serial.println();
            return;
        }

        // Maintenance plane: GW configures/flashes THIS board.
        if (s_tunnel && s_tunnel->handle(type, p, len))
            return;

        switch (type)
        {
        case MEDIA_TEXT:
            if (len >= 1)
                s_huMedia.onMediaText(p[0], (const char*)(p + 1), len - 1, millis());
            return;
        case DISP_CMD:
            handleDispCmd(p, len);
            return;
        case TIME:
            if (len >= 4)
            {
                struct timeval tv = {};
                tv.tv_sec = (time_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                           | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
                settimeofday(&tv, nullptr);
            }
            return;
        default:
            return;   // SIG_BATCH / HU_STATUS: future display screens
        }
    }

    void begin(AffaDisplayBase* display, MediaRouter* router, LinkTunnel* tunnel)
    {
        s_display = display;
        s_router = router;
        s_tunnel = tunnel;
        Serial1.begin(460800, SERIAL_8N1, PIN_RX, PIN_TX);
        s_stream.bind(Serial1);
        s_port.begin(s_stream, defaultClock(), DISP_FW_VER, DISP_CAPS);
        s_port.onMessage(onMsg, nullptr);
        if (s_tunnel)
            s_tunnel->begin(s_port, s_noFs);
        s_up = true;
        LOGI("LINK", "DISP link up on UART1 (TX=%d RX=%d, 460800)", PIN_TX, PIN_RX);
    }

    void service(uint32_t nowMs)
    {
        if (!s_up) return;
        s_huMedia.setNow(nowMs);
        s_huMedia.setLinkUp(s_port.up());
        s_port.service();
    }

    void sendKey(uint16_t affaKey, uint8_t edge)
    {
        if (!s_up) return;
        uint8_t p[3] = { (uint8_t)(affaKey & 0xFF), (uint8_t)(affaKey >> 8), edge };
        s_port.send(KEY_EVT, p, sizeof(p), PRIO_HIGH);
    }

    bool sendRaw(uint8_t type, const uint8_t* payload, uint16_t len)
    {
        return s_up && s_port.send(type, payload, len, PRIO_HIGH);
    }
}
