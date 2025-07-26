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

 
     AffaCommon::AffaError showMenu(const char* header, const char* item1, const char* item2, 
                          uint8_t selectionItem1 = 0x00, uint8_t selectionItem2 = 0x00) ; // Show menu with items

     AffaCommon::AffaError showConfirmBoxWithOffsets(const char* caption, const char* row1, const char* row2) ; // Show confirm box with offsets
     AffaCommon::AffaError showInfoMenu(const char* item1, const char* item2, const char* item3, 
                              uint8_t offset1 = 0x41, uint8_t offset2 = 0x44, uint8_t offset3 = 0x48, 
                              uint8_t infoPrefix = 0x70) ; // Show info menu with items and offsets
};