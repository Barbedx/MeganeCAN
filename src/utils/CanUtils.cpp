#include "CanUtils.h"
#include "../bus/HwCanBus.h"
#include "Log.h"

// The live-bus gate and the @TX serial mirror now live in HwCanBus (the single
// CAN_FRAME<->Frame seam). These statics stay as thin delegators so existing
// callers (CanUtils::busAlive / noteRxActivity / sendFrame) keep working unchanged.
void CanUtils::noteRxActivity()
{
    HwCanBus::instance().noteRxActivity();
}

bool CanUtils::busAlive()
{
    return HwCanBus::instance().isLive();
}

void CanUtils::sendCan(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                       uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7)
{
    CAN_FRAME frame;
    frame.id = id;
    frame.length = 8;
    frame.data.uint8[0] = d0;
    frame.data.uint8[1] = d1;
    frame.data.uint8[2] = d2;
    frame.data.uint8[3] = d3;
    frame.data.uint8[4] = d4;
    frame.data.uint8[5] = d5;
    frame.data.uint8[6] = d6;
    frame.data.uint8[7] = d7;

    CanUtils::sendFrame(frame);
}
void CanUtils::sendFrame(CAN_FRAME &frame)
{
    // Delegate to the one CAN bus. HwCanBus runs the @TX serial mirror (as a tap,
    // before the gate) and the live-bus gate, then transmits on a live bus. Behavior
    // is identical to the old inline path; the conversion lives there now.
    Frame f;
    f.id = frame.id;
    f.extended = frame.extended;
    f.len = frame.length > 8 ? 8 : frame.length;
    for (int i = 0; i < f.len; i++)
        f.data[i] = frame.data.uint8[i];
    HwCanBus::instance().send(f);
}
void CanUtils::sendMsgBuf(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (len != 8)
        return;
    sendCan(id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
}

void CanUtils::printCanFrame(const CAN_FRAME &frame, bool isOutgoing)
{
    // Per-frame human dump — the firehose. TRACE level, so it is silent unless someone
    // sets /api/loglevel?n=4. Build the hex only when it will actually be emitted (the
    // machine @TX/@RX wire frames are a separate, always-on channel in HwCanBus).
    if (!Log::enabled(LogLevel::TRC))
        return;
    char hex[3 * 8 + 1];
    int p = 0;
    for (int i = 0; i < frame.length && i < 8; i++)
        p += snprintf(hex + p, sizeof(hex) - p, "%s%02X", i ? " " : "", frame.data.uint8[i]);
    LOGT("CAN", "%s %03X [%u] %s", isOutgoing ? "TX" : "RX",
         (unsigned)frame.id, (unsigned)frame.length, hex);
}