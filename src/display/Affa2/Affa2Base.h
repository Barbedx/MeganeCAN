#pragma once
#include "Affa2Constants.h"
#include "../AffaDisplayBase.h"
#include <Arduino.h>
#include <queue>

class Affa2Base : public AffaDisplayBase
{
public:
    Affa2Base() { initializeFuncs(); }
    void tick() override;
    void recv(CAN_FRAME *frame) override;
    void processEvents() override;
    AffaCommon::AffaError setText(const char *text, uint8_t digit = 255) override;
    AffaCommon::AffaError setState(bool enabled) override;
    AffaCommon::AffaError setTime(const char *clock) override;

    void onKeyPressed(AffaCommon::AffaKey key, bool isHold) override {}

    AffaCommon::AffaError showMenu(const char *header, const char *item1, const char *item2, uint8_t scrollLockIndicator) override
    {
        return AffaCommon::AffaError::NoError;
    }

    void ProcessKey(AffaCommon::AffaKey key, bool isHold) override {
        if (keyHandler) keyHandler(key, isHold);
    }

private:
    struct KeyEvent { AffaCommon::AffaKey key; bool isHold; };
    std::queue<KeyEvent> _keyQueue;

protected:
    void initializeFuncs() override
    {
        funcsMax = 2;
        funcs = new Affa3Func[funcsMax]{
            {Affa2::PACKET_ID_SETTEXT,      AffaCommon::FuncStatus::IDLE},
            {Affa2::PACKET_ID_DISPLAY_CTRL, AffaCommon::FuncStatus::IDLE}};
    }

    uint8_t getPacketFiller() const override
    {
        return Affa2::PACKET_FILLER;
    }
};
