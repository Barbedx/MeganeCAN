#pragma once

#include <Arduino.h>
#include <can_common.h>
#include <esp32_can.h>

class CanUtils
{
public:
    static void begin();
    static void setReady(bool ready);
    static void tick();

    static bool enqueueFrame(const CAN_FRAME &frame);

    static void sendCan(uint32_t id,
                        uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                        uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7);

    static void sendFrame(CAN_FRAME &frame); 

    static void printCanFrame(const CAN_FRAME &frame, bool isOutgoing);

private:
    static constexpr size_t TX_QUEUE_SIZE = 16;
    static constexpr uint32_t CAN_WARMUP_MS = 1000;

    static CAN_FRAME _txQueue[TX_QUEUE_SIZE];
    static volatile size_t _txHead;
    static volatile size_t _txTail;
    static volatile size_t _txCount;

    static bool _ready;
    static uint32_t _readyAt;

    static bool dequeueFrame(CAN_FRAME &frame);
    static bool isWarmupDone();
};