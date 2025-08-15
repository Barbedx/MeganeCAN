#pragma once

#include <Preferences.h>
#include <WString.h>

#include "../display/IDisplay.h"  // Interface for display abstraction

namespace DisplayCommands {

class Manager {
public:
    Manager(IDisplay& display, Preferences& prefs)
        : _display(display), _prefs(prefs) {}

    void showText(const String& arg, bool save = true);
    void scrollText(const String& arg);
    void setWelcomeText(const String& arg);
    void setTime(const String& arg);
    void setVoltage(int arg);
    
    // New commands for Affa3NAVDisplay  
    void showMenu(const char *caption, const char *name1, const char *name2, uint8_t scrollLockIndicator);
    void setAux();
    void setTextBig(const String& caption, const String& row1, const String& row2);
    void OnKeyPressed(AffaCommon::AffaKey key, bool isHold);

private:
    IDisplay& _display;
    Preferences& _prefs;
};

} // namespace DisplayCommands

