#pragma once
#include "BusTap.h"
#include "../utils/WireProto.h"

// Mirrors every outbound frame to the UART as "@TX <id> <bytes>" so the PC-side
// virtual display ("CAN emulator") can decode the AFFA3 stream. This logic used to
// live inline in CanUtils::sendFrame; the bus refactor moved it here as a TX tap.
// It runs BEFORE the live-bus gate (see HwCanBus::send) so frames are still
// captured when bench TX is suppressed. The 0x3AF sync chatter is skipped to keep
// the channel readable — keep this in sync with tools/serial_proxy.py.
struct SerialMirrorTap : IBusTap {
    void onTx(const Frame& f) override {
        if (f.id != 0x3AF)
            WireProto::emitTx(f.id, f.data, f.len);
    }
};
