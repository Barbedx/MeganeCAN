#pragma once

#include "../IDisplay.h"
#include "Affa3NavConstants.h"


class Affa3NavDisplay : public IDisplay {
public:
    void tick() override;
    void recv(CAN_FRAME *frame) override;

    AffaCommon::AffaError setText(const char *text, uint8_t digit = 255) override; 
    AffaCommon::AffaError setState(bool enabled) override;
    AffaCommon::AffaError setTime(const char *clock) override;
};