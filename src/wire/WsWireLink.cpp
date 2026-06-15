#include "WsWireLink.h"
#include "../utils/WireProto.h"
#include <Arduino.h>

void WsWireLink::attach(PsychicHttpServer& server, const char* path)
{
    _ws.onOpen([this](PsychicWebSocketClient* c) {
        (void)c;
        _clients++;
    });
    _ws.onClose([this](PsychicWebSocketClient* c) {
        (void)c;
        if (_clients > 0) _clients--;
    });
    _ws.onFrame([this](PsychicWebSocketRequest* req, httpd_ws_frame* frame) -> esp_err_t {
        // Inbound PC->fw command line (@KEY/@INJ/@EMU). Copy to a bounded, NUL-
        // terminated buffer and hand to the registered handler.
        if (_cb && frame && frame->payload && frame->len > 0) {
            char buf[96];
            size_t n = frame->len < sizeof(buf) - 1 ? frame->len : sizeof(buf) - 1;
            memcpy(buf, frame->payload, n);
            buf[n] = '\0';
            _cb(buf, _ctx);
        }
        (void)req;
        return ESP_OK;
    });

    server.on(path, &_ws);
}

void WsWireLink::emitLine(const char* line)
{
    if (_clients <= 0) return;     // no peer -> drop (and never touch the socket)
    _ws.sendAll(line);
}

void WsWireLink::emitRxFrame(uint32_t id, const uint8_t* data, uint8_t len)
{
    if (_clients <= 0) return;
    if (id == 0x3AF || id == 0x3CF) return;            // skip high-rate sync chatter

    uint32_t now = millis();
    if (now - _lastRxMs < _rxIntervalMs) return;       // coalesce to ~10 Hz
    _lastRxMs = now;

    char buf[64];
    WireProto::buildFrameLine(buf, sizeof(buf), WireProto::TAG_RX, id, data, len);
    _ws.sendAll(buf);
}
