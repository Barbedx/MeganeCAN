// Step 4: Create Affa3Display.h/.cpp (AFFA3 implementation)
// File: Affa3Display.h
#pragma once
#include "Affa3Constants.h"     /* Constants related to Affa3 */
#include "../AffaDisplayBase.h" /* Base class for Affa displays */

class Affa3Display : public AffaDisplayBase
{
public:
    Affa3Display() { initializeFuncs(); } // ✅ Ensure init logic runs
    void tick() override;
    void recv(CAN_FRAME *frame) override;
    void processEvents() override {};
    AffaCommon::AffaError setText(const char *text, uint8_t digit = 255 /* 0-9, or anything else for none */) override;
    AffaCommon::AffaError setState(bool enabled) override;
    AffaCommon::AffaError setTime(const char *clock) override;
    void onKeyPressed(AffaCommon::AffaKey key, bool isHold)override{
        Serial.println("[WARN] onKeyPressed() called on Affa3Display — not supported.");
    };

    AffaCommon::AffaError showMenu(const char *header, const char *item1, const char *item2, uint8_t scrollLockIndicator) override
    {
        Serial.println("[WARN] showMenu() called on Affa3Display — not supported.");
        return AffaCommon::AffaError::NoError;
    }

protected:
    void initializeFuncs() override
    {
        funcsMax = 2;
        funcs = new Affa3Func[funcsMax]{
            {Affa3::PACKET_ID_SETTEXT, AffaCommon::FuncStatus::IDLE},
            {Affa3::PACKET_ID_DISPLAY_CTRL, AffaCommon::FuncStatus::IDLE}};
    }

protected:
    uint8_t getPacketFiller() const override
    {
        return Affa3::PACKET_FILLER; // its own constant
    }
};
