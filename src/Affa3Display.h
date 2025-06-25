// Step 4: Create Affa3Display.h/.cpp (AFFA3 implementation)
// File: Affa3Display.h
#pragma once
#include "IDisplay.h"
#include "CanUtils.h"
#include "SyncStatus.h"     /* SyncStatus enum and helper functions */
#include "Affa3Constants.h" /* Constants related to Affa3 */

#ifdef HAS_MENU
#include "IMenuDisplay.h"
#endif

class Affa3Display : public IDisplay
#ifdef HAS_MENU
    ,
                     public IMenuDisplay
#endif
{
public:
    void tick() override;
    void recv(CAN_FRAME *frame) override;
    Affa3::Affa3Error setText(const char *text, uint8_t digit = 255 /* 0-9, or anything else for none */) override;
    Affa3::Affa3Error scrollText(const char *text, uint16_t delayMs /* 0-9, or anything else for none */) override;
    Affa3::Affa3Error setState(bool enabled) override;
#ifdef HAS_MENU
    void setMenu(const char *title, const char *items[], uint8_t count) override;
#endif
};
