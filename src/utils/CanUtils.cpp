#include "CanUtils.h"

CAN_FRAME CanUtils::_txQueue[CanUtils::TX_QUEUE_SIZE]{};
volatile size_t CanUtils::_txHead = 0;
volatile size_t CanUtils::_txTail = 0;
volatile size_t CanUtils::_txCount = 0;
portMUX_TYPE CanUtils::_txMux = portMUX_INITIALIZER_UNLOCKED;

bool CanUtils::_ready = false;
uint32_t CanUtils::_readyAt = 0;

void CanUtils::begin()
{
    portENTER_CRITICAL(&_txMux);
    _txHead = 0;
    _txTail = 0;
    _txCount = 0;
    portEXIT_CRITICAL(&_txMux);
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
    return _ready && ((millis() - _readyAt) >= CAN_WARMUP_MS);
}

bool CanUtils::enqueueFrame(const CAN_FRAME &frame)
{
    if (frame.length > 8)
    {
        Serial.printf("[CAN-TX] enqueue rejected: invalid length=%u\n", frame.length);
        return false;
    }

    portENTER_CRITICAL(&_txMux);
    if (_txCount >= TX_QUEUE_SIZE)
    {
        portEXIT_CRITICAL(&_txMux);
        Serial.printf("[CAN-TX] queue full (%u), dropping frame id=0x%03lX\n",
                      (unsigned)TX_QUEUE_SIZE,
                      (unsigned long)frame.id);
        return false;
    }

    _txQueue[_txTail] = frame;
    _txTail = (_txTail + 1) % TX_QUEUE_SIZE;
    _txCount++;
    portEXIT_CRITICAL(&_txMux);
    return true;
}

bool CanUtils::dequeueFrame(CAN_FRAME &frame)
{
    bool dequeued = false;

    portENTER_CRITICAL(&_txMux);
    if (_txCount > 0)
    {
        frame = _txQueue[_txHead];
        _txHead = (_txHead + 1) % TX_QUEUE_SIZE;
        _txCount--;
        dequeued = true;
    }
    portEXIT_CRITICAL(&_txMux);

    return dequeued;
}

size_t CanUtils::queuedCount()
{
    size_t count = 0;
    portENTER_CRITICAL(&_txMux);
    count = _txCount;
    portEXIT_CRITICAL(&_txMux);
    return count;
}

void CanUtils::tick()
{
    if (!isWarmupDone())
        return;

    for (size_t sent = 0; sent < MAX_TX_BURST_PER_TICK; ++sent)
    {
        CAN_FRAME frame{};
        if (!dequeueFrame(frame))
            return;

        if (frame.id != 0x3AF)
            printCanFrame(frame, true);

        CAN0.sendFrame(frame);
    }
}

void CanUtils::sendCan(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
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

void CanUtils::sendMsgBuf(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (len != 8)
        return;
    sendCan(id, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
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
