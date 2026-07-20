#include "MmLinkService.h"
#include "../LinkUi.h"
#include "../LinkPort.h"
#include "../HwSerialLinkStream.h"
#include "../ProgProto.h"
#include "../ProgOtaClient.h"
#include "../../bus/ArduinoClock.h"
#include "../../media/HuLinkMediaSource.h"
#include "../../utils/AppConfig.h"
#include "../../utils/Log.h"
#include <string.h>

using namespace LinkProto;
using ProgProto::OTA_BEGIN;
using ProgProto::OTA_DATA;
using ProgProto::OTA_END;
using ProgProto::OTA_STAT;

namespace MmLink
{
    // ---- state (loop-task owned unless noted) -------------------------------
    static constexpr uint16_t MM_FW_VER = 0x0100;
    static constexpr uint32_t MM_CAPS =             // CR-05 capability bitmap
        Caps::CAP_DISPLAY | Caps::CAP_CAN | Caps::CAP_BLE | Caps::CAP_OTA |
        Caps::CAP_KEYBOARD | Caps::CAP_MEDIA | Caps::CAP_WEB;
    static constexpr int PIN_TX = 21, PIN_RX = 20;   // §3.2 (SuperMini TX/RX pins)

    static bool s_enabled = false;
    static LinkPort s_port;
    static HwSerialLinkStream s_stream;
    static HuLinkMediaSource s_huMedia;
    static Vh::VehicleState s_vstate;

    // HU status cache (from HU_STATUS frames)
    static uint8_t s_huSource = 0, s_huVolume = 0;
    static uint16_t s_huFreq = 0;
    static uint32_t s_huStatusMs = 0;

    // RAW_FRAME counters + a tiny tail for /api/vh (RE visibility)
    static uint32_t s_rawCount = 0;
    struct RawTail { uint8_t bus; uint16_t id; uint8_t len; uint8_t data[16]; };
    static RawTail s_rawTail[8];
    static uint8_t s_rawTailN = 0, s_rawTailIdx = 0;

    static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

    // ---- cross-task mailbox -------------------------------------------------
    enum OpState : uint8_t { OP_IDLE, OP_POSTED, OP_SENT, OP_DONE, OP_FAIL };
    struct Mailbox {
        volatile OpState state = OP_IDLE;
        uint8_t reqType = 0;
        uint8_t expectType = 0;
        uint8_t req[MAX_PAYLOAD]; uint16_t reqLen = 0;
        uint8_t resp[MAX_PAYLOAD]; uint16_t respLen = 0;
        uint32_t sentMs = 0;
    };
    static Mailbox s_op;

    // Small queue for fire-and-forget sends from other tasks (keys).
    struct MiniMsg { uint8_t type; uint8_t len; uint8_t p[8]; };
    static MiniMsg s_miniQ[8];
    static volatile uint8_t s_miniHead = 0, s_miniTail = 0;

    // Peer-OTA: the shared flasher (link/ProgOtaClient), bound in begin().
    static ProgOtaClient s_peerOta;

    // ---- link RX ------------------------------------------------------------
    static void onMsg(uint8_t type, const uint8_t* p, uint16_t len, void*)
    {
        uint32_t now = millis();
        switch (type)
        {
        case SIG_BATCH:
            for (uint16_t i = 0; i + 5 <= len; i += 5)
            {
                int32_t v = (int32_t)((uint32_t)p[i + 1] | ((uint32_t)p[i + 2] << 8)
                          | ((uint32_t)p[i + 3] << 16) | ((uint32_t)p[i + 4] << 24));
                s_vstate.set(p[i], v, now);
            }
            return;
        case MEDIA_TEXT:
            if (len >= 1)
                s_huMedia.onMediaText(p[0], (const char*)(p + 1), len - 1, now);
            return;
        case HU_STATUS:
            if (len >= 5)
            {
                s_huSource = p[1];
                s_huVolume = p[2];
                s_huFreq = (uint16_t)p[3] | ((uint16_t)p[4] << 8);
                s_huStatusMs = now;
            }
            return;
        case RAW_FRAME:
            if (len >= 4)
            {
                s_rawCount++;
                RawTail& t = s_rawTail[s_rawTailIdx];
                t.bus = p[0];
                t.id = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
                t.len = (uint8_t)((len - 4) > 16 ? 16 : (len - 4));
                memcpy(t.data, p + 4, t.len);
                s_rawTailIdx = (s_rawTailIdx + 1) % 8;
                if (s_rawTailN < 8) s_rawTailN++;
            }
            return;
        case LOG:
            LOGI("VH", "%.*s", (int)len, (const char*)p);
            return;
        case OTA_STAT:
            if (s_peerOta.onStat(p, len))
                return;
            break;
        default:
            break;
        }

        // Mailbox reply?
        if (s_op.state == OP_SENT && type == s_op.expectType)
        {
            uint16_t n = len > MAX_PAYLOAD ? MAX_PAYLOAD : len;
            memcpy(s_op.resp, p, n);
            s_op.respLen = n;
            s_op.state = OP_DONE;
        }
    }

    // ---- public: lifecycle --------------------------------------------------
    void begin()
    {
        if (!AppConfig::linkEnabled) return;
        Serial1.begin(460800, SERIAL_8N1, PIN_RX, PIN_TX);
        s_stream.bind(Serial1);
        s_port.begin(s_stream, defaultClock(), MM_FW_VER, MM_CAPS);
        s_port.onMessage(onMsg, nullptr);
        s_peerOta.bind([](uint8_t type, const uint8_t* p, uint16_t n, void*) {
            return s_port.send(type, p, n, PRIO_HIGH);
        }, nullptr);
        s_enabled = true;
        LOGI("LINK", "MM link up on UART1 (TX=%d RX=%d, 460800)", PIN_TX, PIN_RX);
    }

    bool enabled() { return s_enabled; }
    bool up() { return s_enabled && s_port.up(); }
    HuLinkMediaSource& mediaSource() { return s_huMedia; }
    const Vh::VehicleState& vehicleState() { return s_vstate; }

    void service()
    {
        if (!s_enabled) return;
        uint32_t now = millis();
        s_huMedia.setNow(now);
        s_huMedia.setLinkUp(s_port.up());

        // Fire-and-forget queue (keys from the router, any task).
        while (s_miniTail != s_miniHead)
        {
            portENTER_CRITICAL(&s_mux);
            MiniMsg m = s_miniQ[s_miniTail];
            s_miniTail = (uint8_t)((s_miniTail + 1) % 8);
            portEXIT_CRITICAL(&s_mux);
            s_port.send(m.type, m.p, m.len, PRIO_HIGH);
        }

        // Mailbox execute
        if (s_op.state == OP_POSTED)
        {
            if (!s_port.up()) s_op.state = OP_FAIL;
            else
            {
                s_port.send(s_op.reqType, s_op.req, s_op.reqLen, PRIO_HIGH);
                s_op.sentMs = now;
                s_op.state = OP_SENT;
            }
        }
        else if (s_op.state == OP_SENT && now - s_op.sentMs > 2000)
            s_op.state = OP_FAIL;   // VH silent — let the handler time out cleanly

        s_peerOta.service(now);   // peer-flash chunk pump

        s_port.service();
    }

    // ---- fire-and-forget ----------------------------------------------------
    static void miniSend(uint8_t type, const uint8_t* p, uint8_t len)
    {
        portENTER_CRITICAL(&s_mux);
        uint8_t next = (uint8_t)((s_miniHead + 1) % 8);
        if (next != s_miniTail)
        {
            MiniMsg& m = s_miniQ[s_miniHead];
            m.type = type;
            m.len = len;
            memcpy(m.p, p, len);
            s_miniHead = next;
        }
        portEXIT_CRITICAL(&s_mux);
    }

    void sendKey(uint16_t affaKey, uint8_t edge)
    {
        if (!s_enabled) return;
        uint8_t p[3] = { (uint8_t)(affaKey & 0xFF), (uint8_t)(affaKey >> 8), edge };
        miniSend(KEY_EVT, p, sizeof(p));
    }

    void sendTime(uint32_t unixSecs)
    {
        if (!s_enabled) return;
        uint8_t p[4] = { (uint8_t)(unixSecs & 0xFF), (uint8_t)((unixSecs >> 8) & 0xFF),
                         (uint8_t)((unixSecs >> 16) & 0xFF), (uint8_t)((unixSecs >> 24) & 0xFF) };
        miniSend(TIME, p, sizeof(p));
    }

    // ---- blocking ops (httpd task) ------------------------------------------
    static bool runOp(uint8_t reqType, const uint8_t* req, uint16_t reqLen,
                      uint8_t expectType, uint32_t timeoutMs = 2500)
    {
        if (!s_enabled) return false;
        // claim the single slot
        portENTER_CRITICAL(&s_mux);
        if (s_op.state != OP_IDLE && s_op.state != OP_DONE && s_op.state != OP_FAIL)
        {
            portEXIT_CRITICAL(&s_mux);
            return false;   // busy
        }
        s_op.state = OP_IDLE;
        memcpy(s_op.req, req, reqLen);
        s_op.reqLen = reqLen;
        s_op.reqType = reqType;
        s_op.expectType = expectType;
        s_op.respLen = 0;
        s_op.state = OP_POSTED;
        portEXIT_CRITICAL(&s_mux);

        uint32_t t0 = millis();
        while (millis() - t0 < timeoutMs)
        {
            if (s_op.state == OP_DONE) return true;
            if (s_op.state == OP_FAIL) { s_op.state = OP_IDLE; return false; }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        s_op.state = OP_IDLE;
        return false;
    }

    bool cfgGet(const char* key, char* val, size_t valLen)
    {
        uint8_t req[32];
        uint16_t n = 0;
        for (const char* c = key; *c && n < 30; c++) req[n++] = (uint8_t)*c;
        req[n++] = 0;
        if (!runOp(CFG_GET, req, n, CFG_ACK)) return false;
        // resp: {ok, key\0value}
        if (s_op.respLen < 2 || !s_op.resp[0]) { s_op.state = OP_IDLE; return false; }
        uint16_t i = 1;
        while (i < s_op.respLen && s_op.resp[i]) i++;
        i++;
        uint16_t vlen = s_op.respLen > i ? s_op.respLen - i : 0;
        if (vlen >= valLen) vlen = valLen - 1;
        memcpy(val, s_op.resp + i, vlen);
        val[vlen] = 0;
        s_op.state = OP_IDLE;
        return true;
    }

    bool cfgSet(const char* key, const char* val)
    {
        uint8_t req[64];
        uint16_t n = 0;
        for (const char* c = key; *c && n < 30; c++) req[n++] = (uint8_t)*c;
        req[n++] = 0;
        for (const char* c = val; *c && n < 62; c++) req[n++] = (uint8_t)*c;
        bool ok = runOp(CFG_SET, req, n, CFG_ACK)
               && s_op.respLen >= 1 && s_op.resp[0];
        s_op.state = OP_IDLE;
        return ok;
    }

    bool capCtl(uint8_t op, uint8_t mode, uint16_t secs)
    {
        uint8_t req[4] = { op, mode, (uint8_t)(secs & 0xFF), (uint8_t)(secs >> 8) };
        bool ok = runOp(CAP_CTL, req, sizeof(req), CFG_ACK)
               && s_op.respLen >= 1 && s_op.resp[0];
        s_op.state = OP_IDLE;
        return ok;
    }

    bool fileLs(String& json)
    {
        if (!runOp(FILE_LS, nullptr, 0, FILE_LS)) return false;
        json = "[";
        uint16_t i = 0;
        bool first = true;
        while (i + 5 <= s_op.respLen)
        {
            uint32_t size = (uint32_t)s_op.resp[i] | ((uint32_t)s_op.resp[i + 1] << 8)
                          | ((uint32_t)s_op.resp[i + 2] << 16) | ((uint32_t)s_op.resp[i + 3] << 24);
            i += 4;
            uint16_t s = i;
            while (i < s_op.respLen && s_op.resp[i]) i++;
            if (!first) json += ",";
            first = false;
            json += "{\"name\":\"";
            json += String((const char*)(s_op.resp + s)).substring(0, i - s);
            json += "\",\"size\":" + String(size) + "}";
            i++;
        }
        json += "]";
        s_op.state = OP_IDLE;
        return true;
    }

    int fileRead(const char* name, uint32_t offset, uint8_t* buf, size_t maxLen,
                 uint32_t& totalSize)
    {
        totalSize = 0;
        size_t got = 0;
        // Loop link-sized chunks until maxLen is filled or EOF/total reached.
        while (got < maxLen)
        {
            uint8_t req[32];
            uint16_t n = 0;
            uint32_t off = offset + got;
            req[n++] = (uint8_t)(off & 0xFF);
            req[n++] = (uint8_t)((off >> 8) & 0xFF);
            req[n++] = (uint8_t)((off >> 16) & 0xFF);
            req[n++] = (uint8_t)((off >> 24) & 0xFF);
            for (const char* c = name; *c && n < 31; c++) req[n++] = (uint8_t)*c;
            if (!runOp(FILE_REQ, req, n, FILE_DATA)) return got ? (int)got : -1;
            if (s_op.respLen < 8) { s_op.state = OP_IDLE; return got ? (int)got : -1; }
            totalSize = (uint32_t)s_op.resp[4] | ((uint32_t)s_op.resp[5] << 8)
                      | ((uint32_t)s_op.resp[6] << 16) | ((uint32_t)s_op.resp[7] << 24);
            uint16_t dlen = s_op.respLen - 8;
            s_op.state = OP_IDLE;
            if (dlen == 0) break;                     // EOF
            if (got + dlen > maxLen) dlen = (uint16_t)(maxLen - got);
            memcpy(buf + got, s_op.resp + 8, dlen);
            got += dlen;
            if (offset + got >= totalSize) break;
        }
        return (int)got;
    }

    // ---- OTA-over-link (shared ProgOtaClient does the whole dance) ----------
    const char* otaError() { return s_peerOta.error(); }

    bool otaBegin(uint32_t size)
    {
        if (!s_enabled || !up()) return false;
        return s_peerOta.begin(size);
    }

    bool otaWrite(const uint8_t* data, size_t len) { return s_peerOta.write(data, len); }
    bool otaEnd() { return s_peerOta.end(); }

    // ---- status JSON --------------------------------------------------------
    String statusJson()
    {
        uint32_t now = millis();
        String j = "{\"enabled\":";
        j += s_enabled ? "true" : "false";
        j += ",\"up\":";
        j += up() ? "true" : "false";
        if (s_enabled)
        {
            char buf[64];
            snprintf(buf, sizeof(buf),
                     ",\"peerFw\":\"%04X\",\"drops\":%lu,\"gaps\":%lu,\"raw\":%lu",
                     s_port.peerFwVer(),
                     (unsigned long)s_port.txDropped(),
                     (unsigned long)s_port.rxSeqGaps(),
                     (unsigned long)s_rawCount);
            j += buf;
            j += ",\"sigs\":{";
            bool first = true;
            for (uint8_t i = 0; i < Vh::SIG_COUNT; i++)
            {
                if (!s_vstate.has(i)) continue;
                if (!first) j += ",";
                first = false;
                j += "\"";
                j += Vh::sigName(i);
                j += "\":" + String(s_vstate.val[i]);
            }
            j += "}";
            if (s_huStatusMs)
            {
                snprintf(buf, sizeof(buf),
                         ",\"hu\":{\"source\":%u,\"vol\":%u,\"freq\":%u,\"age\":%lu}",
                         s_huSource, s_huVolume, s_huFreq,
                         (unsigned long)(now - s_huStatusMs));
                j += buf;
            }
            if (s_peerOta.active())
                j += ",\"ota\":" + String(s_peerOta.progress());
        }
        j += "}";
        return j;
    }
}

// LinkUi (the role-neutral web surface) — C3/MM role: everything proxies to
// the remote vehicle board over the link.
namespace LinkUi
{
    bool enabled() { return MmLink::enabled(); }
    bool up() { return MmLink::up(); }
    String statusJson() { return MmLink::statusJson(); }
    bool cfgGet(const char* k, char* v, size_t n) { return MmLink::cfgGet(k, v, n); }
    bool cfgSet(const char* k, const char* v) { return MmLink::cfgSet(k, v); }
    bool capCtl(uint8_t op, uint8_t mode, uint16_t secs) { return MmLink::capCtl(op, mode, secs); }
    bool fileLs(String& j) { return MmLink::fileLs(j); }
    int fileRead(const char* n, uint32_t off, uint8_t* b, size_t m, uint32_t& t)
    { return MmLink::fileRead(n, off, b, m, t); }
    bool otaBegin(uint32_t size) { return MmLink::otaBegin(size); }
    bool otaWrite(const uint8_t* d, size_t l) { return MmLink::otaWrite(d, l); }
    bool otaEnd() { return MmLink::otaEnd(); }
    const char* otaError() { return MmLink::otaError(); }
}
