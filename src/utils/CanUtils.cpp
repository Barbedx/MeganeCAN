#include "CanUtils.h"

// Live-bus gate state. lastRxMs == 0 until the first frame is received; the bus
// is considered alive while traffic was seen within BUS_ALIVE_WINDOW_MS.
static volatile uint32_t lastRxMs = 0;
static const uint32_t BUS_ALIVE_WINDOW_MS = 5000;

void CanUtils::noteRxActivity()
{
    lastRxMs = millis();
}

bool CanUtils::busAlive()
{
    return lastRxMs != 0 && (millis() - lastRxMs) < BUS_ALIVE_WINDOW_MS;
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
    if (frame.id != 0x3AF)
    {
        Serial.print("Sending CAN frame: ID=0x");
        Serial.print(frame.id, HEX);
        Serial.print(" Data: ");
        for (int i = 0; i < frame.length; i++)
        {
            Serial.print(frame.data.uint8[i], HEX);
            if (i < frame.length - 1)
            {
                Serial.print(" ");
            }
        }
        Serial.println();
    }
    // Only transmit onto a confirmed-live bus. No RX traffic (e.g. bench board
    // with no transceiver) -> drop the frame so TX never drives the controller
    // BUS_OFF and trips the IDF auto-recovery assert (twai.c:184). On the car
    // bus, frames arrive constantly so this is open within milliseconds.
    if (!busAlive())
    {
        static uint32_t lastWarnMs = 0;
        if (millis() - lastWarnMs > 5000)
        {
            lastWarnMs = millis();
            Serial.println("[CAN] no live bus (no RX) — TX suppressed");
        }
        return;
    }
    CAN0.sendFrame(frame);
}
void CanUtils::sendMsgBuf(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (len != 8)
        return;
    sendCan(id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
}

void CanUtils::printCanFrame(const CAN_FRAME &frame, bool isOutgoing)
{
    const char *direction = isOutgoing ? "[TX]" : "[RX]";
    Serial.print(direction);
    Serial.print(" ID: 0x");
    if (frame.id < 0x100)
        Serial.print("0"); // pad if needed
    Serial.print(frame.id, HEX);
    Serial.print(" Len: ");
    Serial.print(frame.length);
    Serial.print(" Data: { ");
    for (int i = 0; i < frame.length; i++)
    {
        if (frame.data.uint8[i] < 0x10)
            Serial.print("0");
        Serial.print(frame.data.uint8[i], HEX);
        if (i < frame.length - 1)
            Serial.print(" ");
    }
    Serial.println(" }");
}