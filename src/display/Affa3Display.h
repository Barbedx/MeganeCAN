// Step 4: Create Affa3Display.h/.cpp (AFFA3 implementation)
// File: Affa3Display.h
#pragma once
#include "IDisplay.h"
#include "Affa3Constants.h" /* Constants related to Affa3 */

 

class Affa3Display : public IDisplay
 
{
public:
    void tick() override;
    void recv(CAN_FRAME *frame) override;
    Affa3::Affa3Error setText(const char *text, uint8_t digit = 255 /* 0-9, or anything else for none */) override; 
    Affa3::Affa3Error setState(bool enabled) override;
    Affa3::Affa3Error setTime(const char *clock) override;   
};
