#include "DisplayCommands.h"
// Update the path if "ScrollEffect.h" is in a subfolder, e.g.:
#include "../effects/ScrollEffect.h" 
#include <display/Carminat/CarminatDisplay.h>

namespace DisplayCommands
{
    namespace
    {
        bool waitForDisplayIdle(IDisplay &_display, uint32_t timeoutMs, const char *stage)
        {
            const uint32_t deadline = millis() + timeoutMs;
            while (_display.isTxBusy() && static_cast<int32_t>(millis() - deadline) < 0)
            {
                CanUtils::tick();
                _display.serviceTx();
                delay(1);
            }

            if (_display.isTxBusy())
            {
                Serial.printf("[DisplayCommands] %s still busy after %lu ms\n",
                              stage,
                              static_cast<unsigned long>(timeoutMs));
                return false;
            }

            return true;
        }

        void serviceDisplayFor(IDisplay &_display, uint32_t durationMs)
        {
            const uint32_t deadline = millis() + durationMs;
            while (static_cast<int32_t>(millis() - deadline) < 0)
            {
                CanUtils::tick();
                _display.serviceTx();
                delay(1);
            }
        }

        AffaCommon::AffaError sendSettledStaticText(IDisplay &_display,
                                                    const String &arg,
                                                    bool allowWarmupPause)
        {
            if (allowWarmupPause && _display.isCarminat())
                serviceDisplayFor(_display, 120);

            AffaCommon::AffaError textErr = _display.setText(arg.c_str());
            if (_display.isTxBusy())
                waitForDisplayIdle(_display, 500, "setText");

            if (_display.isCarminat())
            {
                serviceDisplayFor(_display, 80);
                AffaCommon::AffaError secondErr = _display.setText(arg.c_str());
                if (secondErr == AffaCommon::AffaError::NoError)
                    textErr = secondErr;
                if (_display.isTxBusy())
                    waitForDisplayIdle(_display, 500, "setText confirm");
            }

            return textErr;
        }
    }

    void Manager::showText(const String &arg, bool save)
    {
        const AffaCommon::AffaError stateErr = _display.setState(true);
        if (stateErr != AffaCommon::AffaError::NoError)
        {
            Serial.printf("[DisplayCommands] setState(true) failed: %u\n",
                          static_cast<unsigned>(stateErr));
        }

        if (_display.isTxBusy())
            waitForDisplayIdle(_display, 400, "setState");

        AffaCommon::AffaError textErr = sendSettledStaticText(_display, arg, true);
        Serial.printf("[DisplayCommands] showText('%s') result=%u\n",
                      arg.c_str(),
                      static_cast<unsigned>(textErr));

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
        if (_display.isTxBusy())
            waitForDisplayIdle(_display, 400, "scrollText pre-roll");
        ScrollEffect(&_display, ScrollDirection::Left, arg.c_str(), 250);
        if (_display.isTxBusy())
            waitForDisplayIdle(_display, 500, "scrollText post-roll");

        _prefs.begin("display", false);
        String lastText = _prefs.getString("lastText", "");
        if (lastText.length() > 0)
        {
            sendSettledStaticText(_display, lastText, false);
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

    void Manager::setVoltage(int arg)
    {
        if (_display.isCarminat()) {
            static_cast<CarminatDisplay*>(&_display)->getMenu().updateFieldExternally("Voltage", 0, arg);
        } 
    }

    void Manager::showMenu(const char *caption, const char *name1, const char *name2, uint8_t scrollLockIndicator)
    {
        _display.showMenu(caption, name1, name2, scrollLockIndicator);
        // If you have a display object, call its showMenu method.
        // Example: _display.showMenu(caption, name1, name2, scrollLockIndicator);
    };

    void Manager::setTextBig(const String &caption, const String &row1, const String &row2)
    {
        (void)caption;
        (void)row1;
        (void)row2;
        Serial.println("[DisplayCommands] setTextBig not implemented for this display");
        //  _display.showConfirmBoxWithOffsets(caption.c_str(), row1.c_str(), row2.c_str());
    } // namespace DisplayCommands

    void Manager::OnKeyPressed(AffaCommon::AffaKey key, bool isHold)
    {
        Serial.printf("[DisplayCommands::OnKeyPressed] key=0x%04X isHold=%d\n",
                      static_cast<uint16_t>(key), isHold);
        _display.ProcessKey(key, isHold);
        Serial.println("[DisplayCommands::OnKeyPressed] ProcessKey done, calling processEvents");
        _display.processEvents();
        Serial.println("[DisplayCommands::OnKeyPressed] processEvents done");
    }

}
