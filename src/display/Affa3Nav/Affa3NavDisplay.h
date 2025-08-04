#pragma once
#include <vector>
#include "Affa3NavConstants.h"
#include "../AffaDisplayBase.h" /* Base class for Affa displays */
#include "Menu/MenuTypes.h"  // Include the shared MenuItemType and MenuItem definitions
 #include <BleKeyboard.h>
class Affa3NavDisplay : public AffaDisplayBase
{
public:
    Affa3NavDisplay()
    {
        initializeFuncs();
        initializeMenu();
    } // ✅ Ensure init logic runs
    BleKeyboard bleKeyboard;
    void tick() override;
    void onKeyPressed(AffaCommon::AffaKey key, bool isHold) override;
    void recv(CAN_FRAME *frame) override;

    AffaCommon::AffaError setText(const char *text, uint8_t digit = 255) override;
    AffaCommon::AffaError setState(bool enabled) override;
    AffaCommon::AffaError setTime(const char *clock) override;
    // Affa3Nav::ScrollLockIndicator::SCROLL_BOTH
    AffaCommon::AffaError showMenu(const char *header, const char *item1, const char *item2, uint8_t scrollLockIndicator = 0x0B) override;

    AffaCommon::AffaError showConfirmBoxWithOffsets(const char *caption, const char *row1, const char *row2); // Show confirm box with offsets
    AffaCommon::AffaError showInfoMenu(const char *item1, const char *item2, const char *item3,
                                       uint8_t offset1 = 0x41, uint8_t offset2 = 0x44, uint8_t offset3 = 0x48,
                                       uint8_t infoPrefix = 0x70); // Show info menu with items and offsets
 
    void begin()  override  {
        bleKeyboard.begin();
}

protected:
    bool menuActive = false;
     int selectedIndex = 0;
    Menu* currentMenu = nullptr;
    Menu mainMenu; 

    void initializeMenu()
{
    mainMenu.items.clear();  // clear vector inside Menu
    mainMenu.clear();
    mainMenu.items.push_back(MenuItem(
        "Voltage", MenuItemType::ReadOnly,
        []() -> String { return String("14.2V"); },
        nullptr,
        nullptr
    ));

    mainMenu.items.push_back(MenuItem(
        "Color", MenuItemType::Selector,
        [this]() -> String { return currentColor; },
        [this](int8_t delta) { cycleColor(delta); },
        nullptr
    ));

    mainMenu.items.push_back(MenuItem(
        "Effect", MenuItemType::Selector,
        [this]() -> String { return currentEffect; },
        [this](int8_t delta) { cycleEffect(delta); },
        nullptr
    ));

    mainMenu.items.push_back(MenuItem(
        "Brightness", MenuItemType::Range,
        [this]() -> String { return String(brightness) + "%"; },
        [this](int8_t delta) { brightness = constrain(brightness + 5 * delta, 0, 100); },
        nullptr
    ));

    mainMenu.items.push_back(MenuItem(
        "Time setting", MenuItemType::Submenu,
        []() -> String { return ""; },
        nullptr,
        [this]() { openTimeSubmenu(); }
    ));

    currentMenu = &mainMenu;  // point currentMenu to mainMenu here
}

    // Method declarations
    String currentColor = "Red";
    String currentEffect = "Lighting";
    int brightness = 50;
    int hour = 12;
    int minute = 0;

    void drawMenu();
    void cycleColor(int8_t delta);
    void cycleEffect(int8_t delta);
    void openTimeSubmenu();

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