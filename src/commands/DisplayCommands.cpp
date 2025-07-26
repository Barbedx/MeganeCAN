#include "DisplayCommands.h"
// Update the path if "ScrollEffect.h" is in a subfolder, e.g.:
#include "../effects/ScrollEffect.h"

namespace DisplayCommands
{

    void Manager::showText(const String &arg, bool save)
    {
        _display.setState(true);
        _display.setText(arg.c_str());
        if (save)
        {
            _prefs.begin("display", false);
            _prefs.putString("lastText", arg);
            _prefs.end();
        }
    }
    void Manager::scrollText(const String &arg)
    {
        _display.setState(true); 
        ScrollEffect(&_display, ScrollDirection::Left, arg.c_str(), 250);

        _prefs.begin("display", false);
        String lastText = _prefs.getString("lastText", "");
        if (lastText.length() > 0)
        {
            _display.setText(lastText.c_str());
        }
        _prefs.end();
    }

    void Manager::setWelcomeText(const String &arg)
    {
        _prefs.begin("display", false);
        _prefs.putString("welcomeText", arg);
        _prefs.end();

        scrollText(arg);
    }

    void Manager::setTime(const String &timeStr)
    {
        // Assuming display.setTime accepts const char*
        _display.setTime(timeStr.c_str());
    }

    void Manager::setMenu(const String &caption, const String &name1, const String &name2)
    {
        throw std::logic_error("setMenu not implemented for Affa3NAVDisplay");
        // _display.setState(true); // Ensure the display is enabled
        // _display.showMenu(caption.c_str(), name1.c_str(), name2.c_str());
    }

    void Manager::setTextBig(const String &caption, const String &row1, const String &row2)
    {
        throw std::logic_error("setTextBig not implemented for Affa3NAVDisplay");
        //  _display.showConfirmBoxWithOffsets(caption.c_str(), row1.c_str(), row2.c_str());
    } // namespace DisplayCommands
}