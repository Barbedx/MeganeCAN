#pragma once
#include <esp32_can.h> /* https://github.com/collin80/esp32_can */
#include "AffaCommonConstants.h" /* Constants related to Affa3 */
#include <utils/CanUtils.h>

class IDisplay {
public:
    virtual void tick() = 0;
    virtual void recv(CAN_FRAME* frame) = 0;
    virtual AffaCommon::AffaError setText(const char* text, uint8_t digit =255 /* 0-9, or anything else for none */) ; 
    virtual AffaCommon::AffaError setState(bool enabled) = 0; 
    virtual AffaCommon::AffaError setTime(const char *clock) = 0;   
};

