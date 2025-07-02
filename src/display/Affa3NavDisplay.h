#pragma once

#include "IDisplay.h"

class Affa3NavDisplay : public IDisplay {
public:
    void tick() override;
    void recv(CAN_FRAME *frame) override;

    Affa3::Affa3Error setText(const char *text, uint8_t digit = 255) override; 
    Affa3::Affa3Error setState(bool enabled) override;
    Affa3::Affa3Error setTime(const char *clock) override;
};