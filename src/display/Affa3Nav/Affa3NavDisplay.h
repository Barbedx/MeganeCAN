#pragma once
#include <vector>
#include "Affa3NavConstants.h"
#include "../AffaDisplayBase.h" /* Base class for Affa displays */
#include "Menu/Menu.h"          // Include the shared MenuItemType and MenuItem definitions
#include <BleKeyboard.h>
class Affa3NavDisplay : public AffaDisplayBase
{
public:
    Affa3NavDisplay()
    : mainMenu(
        "Main Menu",
        [this](const String &h, const String &t, const String &b, uint8_t scroll) {
            this->showMenu(h.c_str(), t.c_str(), b.c_str(), scroll);
        },
        [this](uint8_t row) {
            this->highlightItem(row);
        },
        
        [this]() {
            this->setText("RENAULT", 0); // set defdault  text on close"));
        }
        
    )
    {
        initializeFuncs();
        initializeMenu();
    } // ✅ Ensure init logic runs
    Menu& getMenu()  { return mainMenu; }

    BleKeyboard bleKeyboard;
    void onKeyPressed(AffaCommon::AffaKey key, bool isHold) override;
    void recv(CAN_FRAME *frame) override;
    void processEvents();
    void tick() override;

    AffaCommon::AffaError setText(const char *text, uint8_t digit = 255) override;
    AffaCommon::AffaError setState(bool enabled) override;
    AffaCommon::AffaError setTime(const char *clock) override;
    // Affa3Nav::ScrollLockIndicator::SCROLL_BOTH
    AffaCommon::AffaError showMenu(const char *header, const char *item1, const char *item2, uint8_t scrollLockIndicator = 0x0B) override;

    AffaCommon::AffaError highlightItem(uint8_t id);
    AffaCommon::AffaError showConfirmBoxWithOffsets(const char *caption, const char *row1, const char *row2); // Show confirm box with offsets
    AffaCommon::AffaError showInfoMenu(const char *item1, const char *item2, const char *item3,
                                       uint8_t offset1 = 0x41, uint8_t offset2 = 0x44, uint8_t offset3 = 0x48,
                                       uint8_t infoPrefix = 0x70); // Show info menu with items and offsets
    
     bool isAffa3Nav() const override { return true; }

    void begin() override
    {
        bleKeyboard.begin();
    }
protected: 

    Menu mainMenu; // pointer instead of object
    void initializeMenu()
    { 
        mainMenu.addItem(MenuItem("Voltage", Field(142, "V"), false));
        mainMenu.addItem(MenuItem("Color",   {"Red", "Green", "Blue", "White"},1));
        mainMenu.addItem(MenuItem("Effect",  {"Static", "Blink", "Fade"},2));
        mainMenu.addItem(MenuItem("Power",  Field(163, 0, 500, 1,2, "HP")));
        mainMenu.addItem(MenuItem("Mileage",  Field(250345, 0, 500000, 1,100, "km")));
        mainMenu.addItem(MenuItem("Brightness", Field(50, 0, 100, 5,2, "%")));

        //MenuItem timeItem("Time", { Field(12, 0, 23, 1), Field(34, 0, 59, 1) }); 

        // timeItem.fields[0].onChange = [](Field &f) {

        auto &timeItem = mainMenu.addItem(MenuItem(
            "Time",
            { Field(12, 0, 23, 1,3), Field(34, 0, 59, 1,5) }
        ));
 

        timeItem.onChange = [&](const MenuItem &item) {
            char buf[5];
            snprintf(buf, sizeof(buf), "%02d%02d",
                item.fields[0].intValue,
                item.fields[1].intValue
            );
            Serial.printf("Time changed to: %s\n", buf); 
            setTime(buf);
        };
 
    }

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