#pragma once
#include <Arduino.h>
#include "WireLink.h"

// The UART link: emits each WireProto line over Serial exactly as before, so the
// existing tools/serial_proxy.py keeps parsing the byte-identical @TX stream. Serial
// is always "connected". Inbound serial commands are not wired here (the proxy's
// serial-write path is unreliable on Windows — WsWireLink is the working return
// path); onCommand is accepted but unused.
struct SerialWireLink : WireLink {
    void emitLine(const char* line) override { Serial.println(line); }
    void onCommand(CommandCb cb, void* ctx) override { (void)cb; (void)ctx; }
    bool connected() const override { return true; }
};
