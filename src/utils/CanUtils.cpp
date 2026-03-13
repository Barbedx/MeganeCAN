#include "CanUtils.h"
#include <string.h>

CAN_FRAME CanUtils::_txQueue[CanUtils::TX_QUEUE_SIZE]{};
volatile size_t CanUtils::_txHead = 0;
volatile size_t CanUtils::_txTail = 0;
volatile size_t CanUtils::_txCount = 0;

bool CanUtils::_ready = false;
uint32_t CanUtils::_readyAt = 0;

void CanUtils::begin()
{
    
    _txHead = 0;
    _txTail = 0;
    _txCount = 0;
    _ready = false;
    _readyAt = 0;
}

void CanUtils::setReady(bool ready)
{
    _ready = ready;
    if (ready)
    {
        _readyAt = millis();
        Serial.printf("[CAN-TX] ready=true, warmup=%lu ms\n", (unsigned long)CAN_WARMUP_MS);
    }
    else
    {
        Serial.println("[CAN-TX] ready=false");
    }
}

bool CanUtils::isWarmupDone()
{
    if (!_ready)
        return false;

    return (millis() - _readyAt) >= CAN_WARMUP_MS;
}

bool CanUtils::enqueueFrame(const CAN_FRAME &frame)
{
    if (frame.length > 8)
    {
        Serial.printf("[CAN-TX] enqueue rejected: invalid length=%u\n", frame.length);
        return false;
    }

    if (_txCount >= TX_QUEUE_SIZE)
    {
        Serial.printf("[CAN-TX] queue full, dropping frame id=0x%03lX\n", (unsigned long)frame.id);
        return false;
    }

    _txQueue[_txTail] = frame;
    _txTail = (_txTail + 1) % TX_QUEUE_SIZE;
    _txCount++;
    return true;
}

bool CanUtils::dequeueFrame(CAN_FRAME &frame)
{
    if (_txCount == 0)
        return false;

    frame = _txQueue[_txHead];
    _txHead = (_txHead + 1) % TX_QUEUE_SIZE;
    _txCount--;
    return true;
}

void CanUtils::tick()
{
    if (!_ready)
        return;

    if (!isWarmupDone())
        return;

    CAN_FRAME frame{};
    if (!dequeueFrame(frame))
        return;

    if (frame.length > 8)
    {
        Serial.printf("[CAN-TX] dequeued invalid length=%u\n", frame.length);
        return;
    }

    if (frame.id != 0x3AF)
        printCanFrame(frame, true);

    CAN0.sendFrame(frame);
}

void CanUtils::sendCan(uint32_t id,
                       uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                       uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7)
{
    CAN_FRAME frame{};
    frame.id = id;
    frame.length = 8;
    frame.extended = 0;
    frame.rtr = 0;

    frame.data.uint8[0] = d0;
    frame.data.uint8[1] = d1;
    frame.data.uint8[2] = d2;
    frame.data.uint8[3] = d3;
    frame.data.uint8[4] = d4;
    frame.data.uint8[5] = d5;
    frame.data.uint8[6] = d6;
    frame.data.uint8[7] = d7;

    enqueueFrame(frame);
}

void CanUtils::sendFrame(CAN_FRAME &frame)
{
    CAN_FRAME safeFrame{};
    safeFrame = frame;

    if (safeFrame.length > 8)
    {
        Serial.printf("[CAN-TX] invalid length: %u\n", safeFrame.length);
        return;
    }

    enqueueFrame(safeFrame);
} 

void CanUtils::printCanFrame(const CAN_FRAME &frame, bool isOutgoing)
{
    const char *direction = isOutgoing ? "[TX]" : "[RX]";
    Serial.print(direction);
    Serial.print(" ID: 0x");
    if (frame.id < 0x100)
        Serial.print("0");
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