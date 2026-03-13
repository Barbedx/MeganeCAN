#pragma once
#include "UpdateListConstants.h"
#include "../AffaDisplayBase.h"
#include <Arduino.h>
#include <queue>

class UpdateListBase : public AffaDisplayBase
{
public:
    UpdateListBase() { initializeFuncs(); }
    void recv(CAN_FRAME *frame) override;
    AffaCommon::AffaError setText(const char *text, uint8_t digit = 255) override;
    AffaCommon::AffaError setState(bool enabled) override;
    AffaCommon::AffaError setTime(const char *clock) override;

    void onKeyPressed(AffaCommon::AffaKey key, bool isHold) override {}

    AffaCommon::AffaError showMenu(const char *header, const char *item1, const char *item2, uint8_t scrollLockIndicator) override
    {
        return AffaCommon::AffaError::NoError;
    }

    void ProcessKey(AffaCommon::AffaKey key, bool isHold) override;

protected:
    // Called when the radio sends text on 0x121 — isAux=true when "AUX" detected.
    // Override in subclasses to react (e.g. re-assert track title).
    virtual void onRadioText(bool isAux) { (void)isAux; }

    bool _amsKeysEnabled = true;

private:
    struct KeyEvent { AffaCommon::AffaKey key; bool isHold; };
    std::queue<KeyEvent> _keyQueue;
    char _transientText[13]{};
    uint32_t _transientUntilMs = 0;

protected:
    void processEvents() override;
    void initializeFuncs() override
    {
        funcsMax = 2;
        funcs = new Affa3Func[funcsMax]{
            {UpdateList::PACKET_ID_SETTEXT,      AffaCommon::FuncStatus::IDLE},
            {UpdateList::PACKET_ID_DISPLAY_CTRL, AffaCommon::FuncStatus::IDLE}};
    }
    void sendAliveFrame() override;
    void sendSyncRequestFrame() override;

    uint8_t getPacketFiller() const override
    {
        return UpdateList::PACKET_FILLER;
    }

    void showTransientText(const char *text, uint32_t durationMs);
    bool isTransientActive(uint32_t now) const
    {
        return _transientUntilMs != 0 && now < _transientUntilMs;
    }
};
