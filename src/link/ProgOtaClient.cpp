#include "ProgOtaClient.h"
#include "ProgProto.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

void ProgOtaClient::fail(const char* what, uint32_t detail)
{
    snprintf(_err, sizeof(_err), "%s @%lu", what, (unsigned long)detail);
    _st = ERROR;
    _awaitAck = false;
}

bool ProgOtaClient::onStat(const uint8_t* p, uint16_t len)
{
    if (_st == IDLE || _st == ERROR) return false;
    if (len < 5) return true;
    uint8_t code = p[0];
    uint32_t detail = (uint32_t)p[1] | ((uint32_t)p[2] << 8)
                    | ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
    if (code != 0)
    {
        fail("peer err", detail);
        return true;
    }
    if (_st == BEGIN_REQ)      _st = RUN;
    else if (_st == END_REQ) { _endAcked = true; _st = IDLE; }
    else                     { _acked = detail; _awaitAck = false; }
    return true;
}

void ProgOtaClient::service(uint32_t nowMs)
{
    if (_st == RUN && !_awaitAck && _bufLen > 0 && _send)
    {
        uint16_t avail = (uint16_t)(_bufLen - _bufOff);
        uint16_t n = avail > CHUNK ? CHUNK : avail;
        uint8_t frame[4 + CHUNK];
        uint32_t off = _nextOff;
        frame[0] = (uint8_t)(off & 0xFF);
        frame[1] = (uint8_t)((off >> 8) & 0xFF);
        frame[2] = (uint8_t)((off >> 16) & 0xFF);
        frame[3] = (uint8_t)((off >> 24) & 0xFF);
        memcpy(frame + 4, _buf + _bufOff, n);
        if (_send(ProgProto::OTA_DATA, frame, (uint16_t)(4 + n), _sctx))
        {
            _awaitAck = true;
            _lastTxMs = nowMs;
            _nextOff += n;
            _bufOff = (uint16_t)(_bufOff + n);
            if (_bufOff >= _bufLen) { _bufOff = 0; _bufLen = 0; }
        }
    }
    if (((_st == RUN && _awaitAck) || _st == BEGIN_REQ || _st == END_REQ) &&
        nowMs - _lastTxMs > 5000)
        fail("peer timeout", _nextOff);
}

bool ProgOtaClient::begin(uint32_t size)
{
    if (!_send || active()) return false;
    _err[0] = 0;
    _size = size;
    _nextOff = 0;
    _acked = 0;
    _bufLen = 0;
    _bufOff = 0;
    _awaitAck = false;
    uint8_t p[4] = { (uint8_t)(size & 0xFF), (uint8_t)((size >> 8) & 0xFF),
                     (uint8_t)((size >> 16) & 0xFF), (uint8_t)((size >> 24) & 0xFF) };
    _lastTxMs = millis();
    _st = BEGIN_REQ;
    _send(ProgProto::OTA_BEGIN, p, sizeof(p), _sctx);
    uint32_t t0 = millis();
    while (millis() - t0 < 10000)   // peer erases its slot
    {
        if (_st == RUN) return true;
        if (_st == ERROR) { _st = IDLE; return false; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    snprintf(_err, sizeof(_err), "begin timeout");
    _st = IDLE;
    return false;
}

bool ProgOtaClient::write(const uint8_t* data, size_t len)
{
    while (len > 0)
    {
        if (_st == ERROR) { _st = IDLE; return false; }
        if (_st != RUN) return false;
        if (_bufLen == 0)
        {
            uint16_t n = len > sizeof(_buf) ? (uint16_t)sizeof(_buf) : (uint16_t)len;
            memcpy(_buf, data, n);
            _bufOff = 0;
            _bufLen = n;    // hand over to service()
            data += n;
            len -= n;
        }
        else
            vTaskDelay(pdMS_TO_TICKS(2));
    }
    return true;
}

bool ProgOtaClient::end()
{
    uint32_t t0 = millis();
    while ((_bufLen > 0 || _awaitAck) && _st == RUN && millis() - t0 < 15000)
        vTaskDelay(pdMS_TO_TICKS(5));
    if (_st != RUN) { if (_st == ERROR) _st = IDLE; return false; }
    _endAcked = false;
    _lastTxMs = millis();
    _st = END_REQ;
    _send(ProgProto::OTA_END, nullptr, 0, _sctx);
    t0 = millis();
    while (millis() - t0 < 20000)   // peer verifies + switches boot slot
    {
        if (_endAcked) return true;
        if (_st == ERROR) { _st = IDLE; return false; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    snprintf(_err, sizeof(_err), "end timeout");
    _st = IDLE;
    return false;
}
