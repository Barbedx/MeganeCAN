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


    // New commands for Affa3NAVDisplay  
    void setMenu(const String& caption, const String& name1, const String& name2);
    void setAux();
    void setTextBig(const String& caption, const String& row1, const String& row2);

private:
    IDisplay& _display;
    Preferences& _prefs;
};

} // namespace DisplayCommands

