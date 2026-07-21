#include "GwLink.h"
#include "../link/LinkUi.h"
#include "../link/LinkPort.h"
#include "../link/ProgProto.h"
#include "../link/ProgOtaClient.h"
#include "../link/HwSerialLinkStream.h"
#include "../link/LinkProto.h"
#include "../bus/ArduinoClock.h"
#include "../vh/target/VhConfig.h"
#include "../utils/Log.h"
#include <Arduino.h>
#include <string.h>
#include <stdarg.h>

using namespace LinkProto;

// printf-append into a fixed buffer (single final String alloc — CLAUDE.md
// heap discipline for HTTP responses). Returns the new write offset.
static int addf(char* b, int p, size_t cap, const char* fmt, ...)
{
    if (p < 0 || p >= (int)cap) return p;
    va_list ap;
    va_start(ap, fmt);
    p += vsnprintf(b + p, cap - p, fmt, ap);
    va_end(ap);
    return p;
}

namespace GwLink
{
    static constexpr uint16_t GW_FW_VER = 0x0100;
    static constexpr uint32_t GW_CAPS =
        Caps::CAP_CAN | Caps::CAP_BLE | Caps::CAP_LOGGER | Caps::CAP_OTA |
        Caps::CAP_MEDIA | Caps::CAP_WEB;
    static constexpr int PIN_TX = 18, PIN_RX = 19;   // §3.2 UART2 -> peripheral

    static LinkPort s_port;
    static HwSerialLinkStream s_stream;
    static KeyRouter* s_keys = nullptr;
    static Vh::VehicleDecoder* s_decoder = nullptr;
    static Vh::CanboxEmitter* s_canbox = nullptr;
    static VhFs::FsLogger* s_fs = nullptr;
    static LinkTunnel* s_tunnel = nullptr;
    static bool s_up = false;   // begin() ran

    // Peer-OTA: the shared flasher (link/ProgOtaClient) streams images to the
    // peripheral; bound to this port's send in begin().
    static ProgOtaClient s_peerOta;

    // Peer-CFG mailbox (httpd posts, loop sends, CFG_ACK completes) — how the
    // GW web reaches the peripheral's DispCfg (display_type & co).
    enum PcState : uint8_t { PC_IDLE, PC_POSTED, PC_SENT, PC_DONE, PC_FAIL };
    static volatile PcState s_pc = PC_IDLE;
    static uint8_t s_pcType = 0;
    static uint8_t s_pcReq[64];
    static uint16_t s_pcReqLen = 0;
    static uint8_t s_pcResp[96];
    static volatile uint16_t s_pcRespLen = 0;
    static uint32_t s_pcSentMs = 0;

    // Capture start/stop executed on the loop task (FsLogger's writers live
    // there — no cross-task mutation of its buffer state).
    static volatile uint8_t s_capReq = 0;      // 0 none, 1 start, 2 stop
    static uint8_t s_capMode = 0;
    static uint16_t s_capSecs = 0;
    static volatile int8_t s_capResult = -1;   // -1 pending, 0 fail, 1 ok

    // Unknown HU->box frames (the P4 RE target): live counter + small tail for
    // the web UI — GW hosts the web, so no link streaming needed.
    static uint32_t s_huUnknown = 0;
    struct HuTail { uint8_t cmd; uint8_t len; uint8_t data[16]; };
    static HuTail s_huTail[6];
    static uint8_t s_huTailN = 0, s_huTailIdx = 0;

    static void onMsg(uint8_t type, const uint8_t* p, uint16_t len, void*)
    {
        // Peer-flash acks first (OTA_STAT while we are the flasher).
        if (type == ProgProto::OTA_STAT && s_peerOta.onStat(p, len))
            return;

        // Maintenance plane: someone (the DISP USB tunnel) configuring/flashing GW.
        if (s_tunnel && s_tunnel->handle(type, p, len))
            return;

        switch (type)
        {
        case KEY_EVT:
            // {affaKey u16 LE, edge}: the peripheral's SWC capture. Release
            // edges carry no action today (the router works on press/long).
            if (len >= 3 && s_keys && p[2] != 0)
                s_keys->route((AffaCommon::AffaKey)((uint16_t)p[0] | ((uint16_t)p[1] << 8)),
                              p[2] == 2);
            break;
        case CFG_ACK:
            if (s_pc == PC_SENT)
            {
                uint16_t n = len > sizeof(s_pcResp) ? (uint16_t)sizeof(s_pcResp) : len;
                memcpy(s_pcResp, p, n);
                s_pcRespLen = n;
                s_pc = PC_DONE;
            }
            break;
        case LOG:
            LOGI("IOC", "%.*s", (int)len, (const char*)p);
            break;
        default:
            break;
        }
    }

    void noteHuUnknown(uint8_t cmd, const uint8_t* payload, uint8_t len)
    {
        s_huUnknown++;
        HuTail& t = s_huTail[s_huTailIdx];
        t.cmd = cmd;
        t.len = len > 16 ? 16 : len;
        memcpy(t.data, payload, t.len);
        s_huTailIdx = (uint8_t)((s_huTailIdx + 1) % 6);
        if (s_huTailN < 6) s_huTailN++;
    }

    void begin(KeyRouter* router, Vh::VehicleDecoder* decoder,
               Vh::CanboxEmitter* canbox, VhFs::FsLogger* fsLog, LinkTunnel* tunnel)
    {
        s_keys = router;
        s_decoder = decoder;
        s_canbox = canbox;
        s_fs = fsLog;
        s_tunnel = tunnel;
        Serial2.begin(460800, SERIAL_8N1, PIN_RX, PIN_TX);
        s_stream.bind(Serial2);
        s_port.begin(s_stream, defaultClock(), GW_FW_VER, GW_CAPS);
        s_port.onMessage(onMsg, nullptr);
        static VhConfigAdapter s_cfgAdapter;   // GW exposes the vehicle knobs
        if (tunnel && fsLog)
            tunnel->begin(s_port, *fsLog, s_cfgAdapter);   // GW's own maintenance plane
        s_peerOta.bind([](uint8_t type, const uint8_t* p, uint16_t n, void*) {
            return s_port.send(type, p, n, PRIO_HIGH);
        }, nullptr);
        s_up = true;
        LOGI("LINK", "GW link up on UART2 (TX=%d RX=%d, 460800)", PIN_TX, PIN_RX);
    }

    bool up() { return s_up && s_port.up(); }
    uint16_t peerFwVer() { return s_port.peerFwVer(); }
    uint32_t peerCaps() { return s_port.peerCaps(); }

    bool send(uint8_t type, const uint8_t* payload, uint16_t len)
    {
        return s_up && s_port.send(type, payload, len);
    }

    void sendTime(uint32_t unixSecs)
    {
        uint8_t p[4] = { (uint8_t)(unixSecs & 0xFF), (uint8_t)((unixSecs >> 8) & 0xFF),
                         (uint8_t)((unixSecs >> 16) & 0xFF), (uint8_t)((unixSecs >> 24) & 0xFF) };
        send(TIME, p, sizeof(p));
    }

    void service(uint32_t nowMs)
    {
        if (!s_up) return;
        s_peerOta.service(nowMs);   // peer-flash chunk pump

        // Peer-CFG mailbox
        if (s_pc == PC_POSTED)
        {
            if (!s_port.up()) s_pc = PC_FAIL;
            else
            {
                s_port.send(s_pcType, s_pcReq, s_pcReqLen, PRIO_HIGH);
                s_pcSentMs = nowMs;
                s_pc = PC_SENT;
            }
        }
        else if (s_pc == PC_SENT && nowMs - s_pcSentMs > 2000)
            s_pc = PC_FAIL;

        // Capture start/stop on the loop task (FsLogger writers live here)
        if (s_capReq)
        {
            uint8_t req = s_capReq;
            int8_t r;
            if (req == 1) r = s_fs && s_fs->start(s_capMode, s_capSecs, nowMs) ? 1 : 0;
            else          { if (s_fs) s_fs->stop(); r = 1; }
            s_capResult = r;
            s_capReq = 0;
        }

        s_port.service();
    }
}

// ---- LinkUi — GW role: local vehicle data/config/captures; ota* -> peer -----
namespace LinkUi
{
    using namespace GwLink;

    bool enabled() { return true; }
    bool up() { return GwLink::up(); }

    bool cfgGet(const char* key, char* val, size_t valLen)
    {
        return VhConfig::get(key, val, valLen);
    }

    bool cfgSet(const char* key, const char* val)
    {
        return VhConfig::set(key, val);
    }

    bool capCtl(uint8_t op, uint8_t mode, uint16_t secs)
    {
        // Executed on the loop task (see GwLink::service) so the httpd task
        // never mutates FsLogger state under its writers.
        if (!GwLink::s_fs) return false;
        GwLink::s_capMode = mode;
        GwLink::s_capSecs = secs;
        GwLink::s_capResult = -1;
        GwLink::s_capReq = op == 1 ? 1 : 2;
        uint32_t t0 = millis();
        while (millis() - t0 < 1000)
        {
            int8_t r = GwLink::s_capResult;
            if (r >= 0) return r == 1;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        return false;
    }

    // Peer-CFG: reach the peripheral's ITunnelConfig (DispCfg) over the link.
    static bool peerCfgOp(uint8_t type, const uint8_t* req, uint16_t reqLen)
    {
        if (!GwLink::up() || GwLink::s_pc != GwLink::PC_IDLE) return false;
        memcpy(GwLink::s_pcReq, req, reqLen);
        GwLink::s_pcReqLen = reqLen;
        GwLink::s_pcType = type;
        GwLink::s_pcRespLen = 0;
        GwLink::s_pc = GwLink::PC_POSTED;
        uint32_t t0 = millis();
        while (millis() - t0 < 2500)
        {
            if (GwLink::s_pc == GwLink::PC_DONE) return true;
            if (GwLink::s_pc == GwLink::PC_FAIL) { GwLink::s_pc = GwLink::PC_IDLE; return false; }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        GwLink::s_pc = GwLink::PC_IDLE;
        return false;
    }

    bool peerCfgGet(const char* key, char* val, size_t valLen)
    {
        uint8_t req[32];
        uint16_t n = 0;
        for (const char* c = key; *c && n < 30; c++) req[n++] = (uint8_t)*c;
        req[n++] = 0;
        if (!peerCfgOp(LinkProto::CFG_GET, req, n)) return false;
        // resp: {ok, key\0value}
        bool ok = GwLink::s_pcRespLen >= 2 && GwLink::s_pcResp[0];
        if (ok)
        {
            uint16_t i = 1;
            while (i < GwLink::s_pcRespLen && GwLink::s_pcResp[i]) i++;
            i++;
            uint16_t vlen = GwLink::s_pcRespLen > i ? (uint16_t)(GwLink::s_pcRespLen - i) : 0;
            if (vlen >= valLen) vlen = (uint16_t)(valLen - 1);
            memcpy(val, GwLink::s_pcResp + i, vlen);
            val[vlen] = 0;
        }
        GwLink::s_pc = GwLink::PC_IDLE;
        return ok;
    }

    bool peerCfgSet(const char* key, const char* val)
    {
        uint8_t req[64];
        uint16_t n = 0;
        for (const char* c = key; *c && n < 30; c++) req[n++] = (uint8_t)*c;
        req[n++] = 0;
        for (const char* c = val; *c && n < 62; c++) req[n++] = (uint8_t)*c;
        bool ok = peerCfgOp(LinkProto::CFG_SET, req, n)
               && GwLink::s_pcRespLen >= 1 && GwLink::s_pcResp[0];
        GwLink::s_pc = GwLink::PC_IDLE;
        return ok;
    }

    bool fileLs(String& json)
    {
        if (!GwLink::s_fs) return false;
        VhFs::FsLogger::Entry e[8];
        uint8_t n = GwLink::s_fs->ls(e, 8);
        json = "[";
        for (uint8_t i = 0; i < n; i++)
        {
            if (i) json += ",";
            json += "{\"name\":\"";
            json += e[i].name;
            json += "\",\"size\":" + String(e[i].size) + "}";
        }
        json += "]";
        return true;
    }

    int fileRead(const char* name, uint32_t offset, uint8_t* buf, size_t maxLen,
                 uint32_t& totalSize)
    {
        if (!GwLink::s_fs) { totalSize = 0; return -1; }
        return GwLink::s_fs->readFile(name, offset, buf, maxLen, totalSize);
    }

    const char* otaError() { return s_peerOta.error(); }

    bool otaBegin(uint32_t size)
    {
        // Capability-gated (CR-05): only flash a peer that declared CAP_OTA.
        if (!GwLink::up() || !(GwLink::peerCaps() & LinkProto::Caps::CAP_OTA))
            return false;
        return s_peerOta.begin(size);
    }

    bool otaWrite(const uint8_t* data, size_t len) { return s_peerOta.write(data, len); }
    bool otaEnd() { return s_peerOta.end(); }

    String statusJson()
    {
        // One fixed buffer, one String alloc at the end (CLAUDE.md heap
        // discipline for HTTP responses — this is polled every 2s).
        // httpd runs a single worker task, so the static is single-writer.
        static char b[1280];
        int p = 0;

        p = addf(b, p, sizeof(b),
                 "{\"enabled\":true,\"up\":%s,\"peerFw\":\"%04X\",\"peerCaps\":%lu"
                 ",\"canbox\":%lu,\"cap\":%d,\"raw\":%lu",
                 GwLink::up() ? "true" : "false",
                 GwLink::peerFwVer(),
                 (unsigned long)GwLink::peerCaps(),
                 (unsigned long)(GwLink::s_canbox ? GwLink::s_canbox->framesSent() : 0),
                 (GwLink::s_fs && GwLink::s_fs->active()) ? 1 : 0,
                 (unsigned long)GwLink::s_huUnknown);

        if (GwLink::s_decoder)
        {
            const Vh::VehicleState& s = GwLink::s_decoder->state();
            p = addf(b, p, sizeof(b), ",\"sigs\":{");
            bool first = true;
            for (uint8_t i = 0; i < Vh::SIG_COUNT; i++)
            {
                if (!s.has(i)) continue;
                p = addf(b, p, sizeof(b), "%s\"%s\":%ld",
                         first ? "" : ",", Vh::sigName(i), (long)s.val[i]);
                first = false;
            }
            p = addf(b, p, sizeof(b), "},\"known\":%lu,\"unknown\":%lu",
                     (unsigned long)GwLink::s_decoder->knownFrames(),
                     (unsigned long)GwLink::s_decoder->unknownFrames());
        }

        // Last unknown HU->box frames (the P4 RE window), newest last.
        if (GwLink::s_huTailN)
        {
            p = addf(b, p, sizeof(b), ",\"huTail\":[");
            for (uint8_t k = 0; k < GwLink::s_huTailN; k++)
            {
                uint8_t idx = (uint8_t)((GwLink::s_huTailIdx + 6 - GwLink::s_huTailN + k) % 6);
                const GwLink::HuTail& t = GwLink::s_huTail[idx];
                p = addf(b, p, sizeof(b), "%s\"%02X:", k ? "," : "", t.cmd);
                for (uint8_t i = 0; i < t.len; i++)
                    p = addf(b, p, sizeof(b), "%02X", t.data[i]);
                p = addf(b, p, sizeof(b), "\"");
            }
            p = addf(b, p, sizeof(b), "]");
        }

        if (s_peerOta.active())
            p = addf(b, p, sizeof(b), ",\"ota\":%lu", (unsigned long)s_peerOta.progress());
        addf(b, p, sizeof(b), "}");
        return String(b);
    }
}
