#include "WsWireLink.h"
#include "../utils/WireProto.h"
#include "../utils/Log.h"
#include <Arduino.h>
#include <string.h>

void WsWireLink::attach(PsychicHttpServer& server, const char* path)
{
    // Log heap on connect/disconnect: a /wire WS client is the single biggest heap
    // consumer (per-socket buffers + send churn), so make its cost visible every
    // time — turns "why is memory low?" into a one-line answer, no guessing.
    _ws.onOpen([this](PsychicWebSocketClient* c) {
        (void)c; _clients++;
        LOGI("WS", "client connected (%d) free=%u maxblk=%u",
             _clients, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    });
    _ws.onClose([this](PsychicWebSocketClient* c) {
        (void)c; if (_clients > 0) _clients--;
        LOGI("WS", "client closed (%d) free=%u maxblk=%u",
             _clients, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    });
    _ws.onFrame([this](PsychicWebSocketRequest* req, httpd_ws_frame* frame) -> esp_err_t {
        // Inbound PC->fw command line(s). Copy to a bounded NUL-terminated buffer and
        // hand each line to the registered handler (@KEY/@INJ/@EMU).
        if (_cb && frame && frame->payload && frame->len > 0) {
            char b[96];
            size_t n = frame->len < sizeof(b) - 1 ? frame->len : sizeof(b) - 1;
            memcpy(b, frame->payload, n);
            b[n] = '\0';
            _cb(b, _ctx);
        }
        (void)req;
        return ESP_OK;
    });
    server.on(path, &_ws);
}

void WsWireLink::append(const char* s, int n)
{
    if (_clients <= 0) return;                    // no peer -> drop, don't accumulate
    portENTER_CRITICAL(&_mux);
    if (_len + n + 1 <= BUF_SIZE) {
        memcpy(_buf + _len, s, n);
        _len += n;
        _buf[_len++] = '\n';
    } else {
        _dropped++;                              // overflow between flushes
    }
    portEXIT_CRITICAL(&_mux);
}

void WsWireLink::emitLine(const char* line)
{
    append(line, (int)strlen(line));
}

void WsWireLink::emitRxFrame(uint32_t id, const uint8_t* data, uint8_t len)
{
    if (_clients <= 0) return;
    if (id == 0x3AF || id == 0x3CF) return;      // skip high-rate sync chatter
    char buf[64];
    int n = WireProto::buildFrameLine(buf, sizeof(buf), WireProto::TAG_RX, id, data, len);
    append(buf, n);
}

void WsWireLink::loop()
{
    if (_clients <= 0) {
        if (_len) { portENTER_CRITICAL(&_mux); _len = 0; portEXIT_CRITICAL(&_mux); }
        return;
    }

    uint32_t now = millis();
    bool due = (now - _lastFlush) >= FLUSH_MS;
    if (_len == 0 || (!due && _len < FLUSH_HI)) return;

    // Snapshot + reset under the lock (fast memcpy), send outside it.
    int n;
    portENTER_CRITICAL(&_mux);
    n = _len;
    memcpy(_tx, _buf, n);
    _len = 0;
    portEXIT_CRITICAL(&_mux);

    _tx[n] = '\0';
    _ws.sendAll(_tx);
    _lastFlush = now;
}
