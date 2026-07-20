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

    // Peer-OTA: the shared flasher (link/ProgOtaClient) streams images to the
    // peripheral; bound to this port's send in begin().
    static ProgOtaClient s_peerOta;

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
        if (s_peerOta.active())
            j += ",\"ota\":" + String(s_peerOta.progress());
        j += "}";
        return j;
    }
}
