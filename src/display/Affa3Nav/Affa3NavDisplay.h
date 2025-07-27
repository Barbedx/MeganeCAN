#pragma once

#include "Affa3NavConstants.h"
#include "../AffaDisplayBase.h" /* Base class for Affa displays */

class Affa3NavDisplay : public AffaDisplayBase
{
public:
    void tick() override;
    void recv(CAN_FRAME *frame) override;

    AffaCommon::AffaError setText(const char *text, uint8_t digit = 255) override;
    AffaCommon::AffaError setState(bool enabled) override;
    AffaCommon::AffaError setTime(const char *clock) override;

    AffaCommon::AffaError showMenu(const char *header, const char *item1, const char *item2, Affa3Nav::ScrollLockIndicator scrollLockIndicator = Affa3Nav::ScrollLockIndicator::SCROLL_BOTH);

    AffaCommon::AffaError showConfirmBoxWithOffsets(const char *caption, const char *row1, const char *row2); // Show confirm box with offsets
    AffaCommon::AffaError showInfoMenu(const char *item1, const char *item2, const char *item3,
                                       uint8_t offset1 = 0x41, uint8_t offset2 = 0x44, uint8_t offset3 = 0x48,
                                       uint8_t infoPrefix = 0x70); // Show info menu with items and offsets

protected:
    uint8_t getPacketFiller() const override
    {
        return Affa3Nav::PACKET_FILLER; // your specific constant
    }

    void initializeFuncs() override
    {
        funcsMax = 2;
        funcs = new Affa3Func[funcsMax]{
            {Affa3Nav::PACKET_ID_SETTEXT, AffaCommon::FuncStatus::IDLE},
            {Affa3Nav::PACKET_ID_NAV, AffaCommon::FuncStatus::IDLE}};
    }
};