#include "LinkTunnel.h"
#include "VhConfig.h"
#include "../../link/ProgProto.h"
#include <Arduino.h>
#include <Update.h>
#include <string.h>

using namespace LinkProto;
using ProgProto::OTA_BEGIN;
using ProgProto::OTA_DATA;
using ProgProto::OTA_END;
using ProgProto::OTA_STAT;

void LinkTunnel::begin(LinkPort& link, VhFs::FsLogger& fs)
{
    _link = &link;
    _fs = &fs;
}

void LinkTunnel::cfgAck(bool ok, const char* key, const char* value)
{
    uint8_t p[MAX_PAYLOAD];
    uint16_t n = 0;
    p[n++] = ok ? 1 : 0;
    for (const char* c = key; *c && n < MAX_PAYLOAD - 1; c++) p[n++] = (uint8_t)*c;
    p[n++] = 0;
    for (const char* c = value; *c && n < MAX_PAYLOAD; c++) p[n++] = (uint8_t)*c;
    _link->send(CFG_ACK, p, n);
}

void LinkTunnel::otaStat(uint8_t code, uint32_t detail)
{
    uint8_t p[5] = { code,
                     (uint8_t)(detail & 0xFF), (uint8_t)((detail >> 8) & 0xFF),
                     (uint8_t)((detail >> 16) & 0xFF), (uint8_t)((detail >> 24) & 0xFF) };
    _link->send(OTA_STAT, p, sizeof(p));
}

static uint32_t rdU32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool LinkTunnel::handle(uint8_t type, const uint8_t* p, uint16_t len)
{
    if (!_link) return false;

    switch (type)
    {
    case CFG_GET:
    {
        // payload: key\0 (a lone NUL-less key is tolerated)
        char key[24] = {};
        strncpy(key, (const char*)p, len < sizeof(key) - 1 ? len : sizeof(key) - 1);
        char val[16];
        bool ok = VhConfig::get(key, val, sizeof(val));
        cfgAck(ok, key, ok ? val : "");
        return true;
    }
    case CFG_SET:
    {
        char key[24] = {}, val[24] = {};
        uint16_t nul = 0;
        while (nul < len && p[nul]) nul++;
        strncpy(key, (const char*)p, nul < sizeof(key) - 1 ? nul : sizeof(key) - 1);
        if (nul + 1 < len)
        {
            uint16_t vlen = len - nul - 1;
            strncpy(val, (const char*)(p + nul + 1),
                    vlen < sizeof(val) - 1 ? vlen : sizeof(val) - 1);
        }
        bool ok = VhConfig::set(key, val);
        cfgAck(ok, key, val);
        return true;
    }
    case CAP_CTL:
    {
        if (len < 4) { cfgAck(false, "cap", "short"); return true; }
        uint8_t op = p[0], mode = p[1];
        uint16_t secs = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
        bool ok;
        if (op == 1) ok = _fs->start(mode, secs, millis());
        else         { _fs->stop(); ok = true; }
        cfgAck(ok, "cap", _fs->active() ? "1" : "0");
        return true;
    }
    case FILE_LS:
    {
        VhFs::FsLogger::Entry e[8];
        uint8_t n = _fs->ls(e, 8);
        uint8_t out[MAX_PAYLOAD];
        uint16_t o = 0;
        for (uint8_t i = 0; i < n; i++)
        {
            uint8_t nameLen = (uint8_t)strlen(e[i].name);
            if (o + 4 + nameLen + 1 > MAX_PAYLOAD) break;
            out[o++] = (uint8_t)(e[i].size & 0xFF);
            out[o++] = (uint8_t)((e[i].size >> 8) & 0xFF);
            out[o++] = (uint8_t)((e[i].size >> 16) & 0xFF);
            out[o++] = (uint8_t)((e[i].size >> 24) & 0xFF);
            memcpy(out + o, e[i].name, nameLen);
            o = (uint16_t)(o + nameLen);
            out[o++] = 0;
        }
        _link->send(FILE_LS, out, o);
        return true;
    }
    case FILE_REQ:
    {
        if (len < 5) return true;
        uint32_t off = rdU32(p);
        char name[24] = {};
        uint16_t nlen = len - 4;
        strncpy(name, (const char*)(p + 4),
                nlen < sizeof(name) - 1 ? nlen : sizeof(name) - 1);

        uint8_t out[MAX_PAYLOAD];
        uint32_t total = 0;
        constexpr uint16_t CHUNK = MAX_PAYLOAD - 8;   // 115 data bytes
        int n = _fs->readFile(name, off, out + 8, CHUNK, total);
        if (n < 0) { total = 0; n = 0; }              // missing file: empty EOF
        out[0] = (uint8_t)(off & 0xFF);
        out[1] = (uint8_t)((off >> 8) & 0xFF);
        out[2] = (uint8_t)((off >> 16) & 0xFF);
        out[3] = (uint8_t)((off >> 24) & 0xFF);
        out[4] = (uint8_t)(total & 0xFF);
        out[5] = (uint8_t)((total >> 8) & 0xFF);
        out[6] = (uint8_t)((total >> 16) & 0xFF);
        out[7] = (uint8_t)((total >> 24) & 0xFF);
        _link->send(FILE_DATA, out, (uint16_t)(8 + n));
        return true;
    }
    case OTA_BEGIN:
    {
        if (len < 4) { otaStat(1, 0); return true; }
        uint32_t size = rdU32(p);
        if (_otaActive) Update.abort();
        if (!Update.begin(size))
        {
            otaStat(1, Update.getError());
            _otaActive = false;
            return true;
        }
        _otaActive = true;
        _otaSize = size;
        _otaOff = 0;
        Serial.printf("[ota] begin %lu bytes\n", (unsigned long)size);
        otaStat(0, 0);
        return true;
    }
    case OTA_DATA:
    {
        if (!_otaActive || len < 4) { otaStat(1, 0); return true; }
        uint32_t off = rdU32(p);
        if (off != _otaOff)
        {
            // Out of order (lost frame / MM retry): tell it where to resume.
            otaStat(2, _otaOff);
            return true;
        }
        uint16_t dlen = len - 4;
        if (Update.write((uint8_t*)(p + 4), dlen) != dlen)
        {
            otaStat(1, Update.getError());
            Update.abort();
            _otaActive = false;
            return true;
        }
        _otaOff += dlen;
        otaStat(0, _otaOff);                          // stop-and-wait ack
        return true;
    }
    case OTA_END:
    {
        if (!_otaActive) { otaStat(1, 0); return true; }
        _otaActive = false;
        if (_otaOff != _otaSize || !Update.end(true))
        {
            otaStat(1, Update.getError());
            Update.abort();
            return true;
        }
        Serial.println("[ota] complete, rebooting into the new image");
        otaStat(0, _otaOff);
        _rebootAtMs = millis() + 500;                 // let the ack drain first
        return true;
    }
    default:
        return false;
    }
}

void LinkTunnel::service(uint32_t nowMs)
{
    if (_rebootAtMs && nowMs >= _rebootAtMs)
    {
        if (_link) _link->service();                  // final drain
        delay(50);
        ESP.restart();
    }
}
