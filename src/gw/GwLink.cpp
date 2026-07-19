#include "GwLink.h"
#include "../link/LinkUi.h"
#include "../link/LinkPort.h"
#include "../link/ProgProto.h"
#include "../link/HwSerialLinkStream.h"
#include "../link/LinkProto.h"
#include "../bus/ArduinoClock.h"
#include "../vh/target/VhConfig.h"
#include "../utils/Log.h"
#include <Arduino.h>
#include <string.h>

using namespace LinkProto;

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

    // ---- peer-OTA (flashing the peripheral over the link, httpd -> loop) ----
    enum PeerOtaState : uint8_t { PO_IDLE, PO_BEGIN_REQ, PO_RUN, PO_END_REQ, PO_ERROR };
    static volatile PeerOtaState s_po = PO_IDLE;
    static volatile bool s_poBeginAcked = false, s_poEndAcked = false;
    static uint32_t s_poSize = 0;
    static uint8_t  s_poBuf[2048];
    static volatile uint16_t s_poBufLen = 0;
    static uint16_t s_poBufOff = 0;
    static uint32_t s_poNextOff = 0;
    static volatile bool s_poAwaitAck = false;
    static uint32_t s_poLastTxMs = 0;
    static char s_poErr[32] = "";
    constexpr uint16_t PO_CHUNK = 112;

    static void onMsg(uint8_t type, const uint8_t* p, uint16_t len, void*)
    {
        // Peer-flash acks first (OTA_STAT while we are the flasher).
        if (type == ProgProto::OTA_STAT && s_po != PO_IDLE)
        {
            if (len < 5) return;
            uint8_t code = p[0];
            uint32_t detail = (uint32_t)p[1] | ((uint32_t)p[2] << 8)
                            | ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
            if (code != 0)
            {
                snprintf(s_poErr, sizeof(s_poErr), "peer err %u @%lu",
                         code, (unsigned long)detail);
                s_po = PO_ERROR;
                s_poAwaitAck = false;
                return;
            }
            if (s_po == PO_BEGIN_REQ) { s_poBeginAcked = true; s_po = PO_RUN; }
            else if (s_po == PO_END_REQ) { s_poEndAcked = true; s_po = PO_IDLE; }
            else s_poAwaitAck = false;   // chunk acked; detail = next offset
            return;
        }

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
        case LOG:
            LOGI("IOC", "%.*s", (int)len, (const char*)p);
            break;
        default:
            break;
        }
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
        if (tunnel && fsLog)
            tunnel->begin(s_port, *fsLog);   // GW's own maintenance plane
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

        // Peer-OTA chunk pump (stop-and-wait out of the shared buffer).
        if (s_po == PO_RUN && !s_poAwaitAck && s_poBufLen > 0)
        {
            uint16_t avail = (uint16_t)(s_poBufLen - s_poBufOff);
            uint16_t n = avail > PO_CHUNK ? PO_CHUNK : avail;
            uint8_t frame[4 + PO_CHUNK];
            uint32_t off = s_poNextOff;
            frame[0] = (uint8_t)(off & 0xFF);
            frame[1] = (uint8_t)((off >> 8) & 0xFF);
            frame[2] = (uint8_t)((off >> 16) & 0xFF);
            frame[3] = (uint8_t)((off >> 24) & 0xFF);
            memcpy(frame + 4, s_poBuf + s_poBufOff, n);
            if (s_port.send(ProgProto::OTA_DATA, frame, (uint16_t)(4 + n), PRIO_HIGH))
            {
                s_poAwaitAck = true;
                s_poLastTxMs = nowMs;
                s_poNextOff += n;
                s_poBufOff = (uint16_t)(s_poBufOff + n);
                if (s_poBufOff >= s_poBufLen) { s_poBufOff = 0; s_poBufLen = 0; }
            }
        }
        if ((s_po == PO_RUN && s_poAwaitAck) || s_po == PO_BEGIN_REQ || s_po == PO_END_REQ)
        {
            if (nowMs - s_poLastTxMs > 5000)
            {
                snprintf(s_poErr, sizeof(s_poErr), "peer timeout @%lu",
                         (unsigned long)s_poNextOff);
                s_po = PO_ERROR;
            }
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
        if (!GwLink::s_fs) return false;
        if (op == 1) return GwLink::s_fs->start(mode, secs, millis());
        GwLink::s_fs->stop();
        return true;
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

    const char* otaError() { return s_poErr; }

    bool otaBegin(uint32_t size)
    {
        if (!GwLink::up() || s_po != PO_IDLE) return false;
        if (!(GwLink::peerCaps() & LinkProto::Caps::CAP_OTA))
        {
            snprintf(s_poErr, sizeof(s_poErr), "peer has no OTA cap");
            return false;
        }
        s_poErr[0] = 0;
        s_poSize = size;
        s_poNextOff = 0;
        s_poBufLen = 0;
        s_poBufOff = 0;
        s_poAwaitAck = false;
        s_poBeginAcked = false;
        uint8_t p[4] = { (uint8_t)(size & 0xFF), (uint8_t)((size >> 8) & 0xFF),
                         (uint8_t)((size >> 16) & 0xFF), (uint8_t)((size >> 24) & 0xFF) };
        s_poLastTxMs = millis();
        s_po = PO_BEGIN_REQ;
        GwLink::send(ProgProto::OTA_BEGIN, p, sizeof(p));
        uint32_t t0 = millis();
        while (millis() - t0 < 10000)   // peer erases its slot
        {
            if (s_po == PO_RUN) return true;
            if (s_po == PO_ERROR) { s_po = PO_IDLE; return false; }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        snprintf(s_poErr, sizeof(s_poErr), "begin timeout");
        s_po = PO_IDLE;
        return false;
    }

    bool otaWrite(const uint8_t* data, size_t len)
    {
        while (len > 0)
        {
            if (s_po == PO_ERROR) { s_po = PO_IDLE; return false; }
            if (s_po != PO_RUN) return false;
            if (s_poBufLen == 0)
            {
                uint16_t n = len > sizeof(s_poBuf) ? sizeof(s_poBuf) : (uint16_t)len;
                memcpy(s_poBuf, data, n);
                s_poBufOff = 0;
                s_poBufLen = n;   // hand over to service()
                data += n;
                len -= n;
            }
            else
                vTaskDelay(pdMS_TO_TICKS(2));
        }
        return true;
    }

    bool otaEnd()
    {
        uint32_t t0 = millis();
        while ((s_poBufLen > 0 || s_poAwaitAck) && s_po == PO_RUN &&
               millis() - t0 < 15000)
            vTaskDelay(pdMS_TO_TICKS(5));
        if (s_po != PO_RUN) { if (s_po == PO_ERROR) s_po = PO_IDLE; return false; }
        s_poEndAcked = false;
        s_poLastTxMs = millis();
        s_po = PO_END_REQ;
        GwLink::send(ProgProto::OTA_END, nullptr, 0);
        t0 = millis();
        while (millis() - t0 < 20000)   // peer verifies + switches boot slot
        {
            if (s_poEndAcked) return true;
            if (s_po == PO_ERROR) { s_po = PO_IDLE; return false; }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        snprintf(s_poErr, sizeof(s_poErr), "end timeout");
        s_po = PO_IDLE;
        return false;
    }

    String statusJson()
    {
        String j = "{\"enabled\":true,\"up\":";
        j += GwLink::up() ? "true" : "false";
        char buf[96];
        snprintf(buf, sizeof(buf), ",\"peerFw\":\"%04X\",\"peerCaps\":%lu,\"canbox\":%lu",
                 GwLink::peerFwVer(),
                 (unsigned long)GwLink::peerCaps(),
                 (unsigned long)(GwLink::s_canbox ? GwLink::s_canbox->framesSent() : 0));
        j += buf;
        j += ",\"cap\":";
        j += (GwLink::s_fs && GwLink::s_fs->active()) ? "1" : "0";
        if (GwLink::s_decoder)
        {
            const Vh::VehicleState& s = GwLink::s_decoder->state();
            j += ",\"sigs\":{";
            bool first = true;
            for (uint8_t i = 0; i < Vh::SIG_COUNT; i++)
            {
                if (!s.has(i)) continue;
                if (!first) j += ",";
                first = false;
                j += "\"";
                j += Vh::sigName(i);
                j += "\":" + String(s.val[i]);
            }
            j += "}";
            snprintf(buf, sizeof(buf), ",\"known\":%lu,\"unknown\":%lu",
                     (unsigned long)GwLink::s_decoder->knownFrames(),
                     (unsigned long)GwLink::s_decoder->unknownFrames());
            j += buf;
        }
        if (s_po == PO_RUN)
            j += ",\"ota\":" + String(s_poNextOff);
        j += "}";
        return j;
    }
}
