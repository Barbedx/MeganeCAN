#pragma once
#include <stdint.h>

// L2 transport seam for the WireProto contract (the @TX/@RX/@EV fw->PC stream and the
// @KEY/@INJ/@EMU PC->fw commands). One link = one channel: SerialWireLink mirrors to
// the UART (the existing serial proxy), WsWireLink broadcasts over a WebSocket to a
// phone/browser (wireless live view + recording) and feeds inbound commands back.
// WireProto fans outbound lines to every registered link.
struct WireLink {
    using CommandCb = void (*)(const char* line, void* ctx);

    virtual ~WireLink() = default;

    // Send one already-formatted line (fw -> PC). Implementations drop it when no
    // peer is connected.
    virtual void emitLine(const char* line) = 0;

    // Register the handler for inbound command lines (PC -> fw). The link invokes it
    // with a NUL-terminated line (e.g. "@INJ 3CF 61 11").
    virtual void onCommand(CommandCb cb, void* ctx) = 0;

    // True when a peer is connected (so callers can skip formatting work).
    virtual bool connected() const = 0;
};
