#pragma once
#pragma once
#include <can_common.h>
#include <esp32_can.h>
#include <Arduino.h>

class CanUtils {
public:
    static void sendCan(uint32_t id, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                        uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7);
    
    static void sendMsgBuf(uint32_t id, const uint8_t* data, uint8_t len = 8);
    static void sendFrame(CAN_FRAME &frame);
};