#include "DispLink.h"
#include "DispCmdServer.h"
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
    static DispCfg s_cfg;   // GW-settable display keys (reboot-on-change)
    static bool s_up = false;

    HuLinkMediaSource& mediaSource() { return s_huMedia; }
    DispCfg& cfg() { return s_cfg; }
    bool up() { return s_up && s_port.up(); }

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
            if (s_display)
                DispCmd::execute(*s_display, p, len);
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
            s_tunnel->begin(s_port, s_noFs, s_cfg);
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
