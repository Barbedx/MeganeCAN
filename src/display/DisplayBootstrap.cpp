#include "display/DisplayBootstrap.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>

#include "effects/ScrollEffect.h"
#include "app/AppContext.h"
#include "ElmManager/MyELMManager.h"
#include "server/HttpServerManager.h"
#include "display/UpdateList/UpdateListDisplay.h"
#include "display/UpdateList/UpdateListMenuDisplay.h"
#include "display/UpdateList/UpdateListBase.h"
#include "display/Carminat/CarminatDisplay.h"

// globals from main.cpp
extern AppContext g_app;

namespace
{
    void restoreDisplayImpl(IDisplay &display, Preferences &prefs)
    {
        prefs.begin("display", true);
        bool autoRestore = prefs.getBool("autoRestore", false);
        if (!autoRestore)
        {
            prefs.end();
            Serial.println("Auto restore disabled by setting.");
            return;
        }

        Serial.println("Auto restore getted and is true.");
        String savedText = prefs.getString("lastText", "");
        String welcomeText = prefs.getString("welcomeText", "");
        prefs.end();

        display.setState(true);

        if (welcomeText.length() > 0)
        {
            ScrollEffect(&display, ScrollDirection::Left, welcomeText.c_str(), 250);
        }
        else
        {
            ScrollEffect(&display, ScrollDirection::Left, "                  Welcome to MEGANE 2", 250);
        }

        if (savedText.length() > 0)
        {
            display.setText(savedText.c_str());
        }
        else
        {
            if (random(0, 2) == 0)
                display.setText("MEGANE");
            else
                display.setText("RENAULT");
        }
    }
}

namespace DisplayBootstrap
{
    void init()
    {
        Serial.println("[NVS] Opening 'config' namespace...");
        Preferences prefs;
        bool nvsOk = prefs.begin("config", false);
        Serial.printf("[NVS] prefs.begin('config') returned: %s\n",
                      nvsOk ? "OK" : "FAILED - namespace missing, all values will be defaults!");

        String displayType = prefs.getString("display_type", "carminat");
        g_app.autoTime = prefs.getBool("auto_time", true);
        g_app.elmEnabled = prefs.getBool("elm_enabled", false);
        bool skipFuncReg = prefs.getBool("skip_funcreg", false);
        prefs.end();

        Serial.println("[Display Init] Display type: " + displayType);
        Serial.println("[Display Init] Auto-time: " + String(g_app.autoTime ? "on" : "off"));
        Serial.println("[Display Init] ELM enabled: " + String(g_app.elmEnabled ? "yes" : "no"));
        Serial.printf("[Display Init] skip_funcreg raw value from NVS: %s\n",
                      skipFuncReg ? "TRUE" : "FALSE (defaulted)");

        if (displayType == "carminat")
        {
            Serial.println("[Display Init] Instantiating CarminatDisplay");
            g_app.display = new CarminatDisplay();
        }
        else if (displayType == "updatelist")
        {
            Serial.println("[Display Init] Instantiating UpdateListDisplay (8-segment)");
            g_app.display = new UpdateListDisplay();
        }
        else if (displayType == "updatelist_menu")
        {
            Serial.println("[Display Init] Instantiating UpdateListMenuDisplay (full LED)");
            g_app.display = new UpdateListMenuDisplay();
        }
        else
        {
            Serial.println("[Display Init] Instantiating UpdateListBase (fallback)");
            g_app.display = new UpdateListBase();
        }

        g_app.display->setSkipFuncReg(skipFuncReg);
        Serial.println("[Display Init] Skip func-reg: " + String(skipFuncReg ? "yes" : "no"));

        g_app.elmManager = new MyELMManager();
        g_app.serverManager = new HttpServerManager(*g_app.display, g_app.preferences);
        g_app.serverManager->attachElm(g_app.elmManager);
        g_app.elmManager->loadHeaderConfig(g_app.preferences);

        if (g_app.display->isCarminat())
            static_cast<CarminatDisplay*>(g_app.display)->attachElm(g_app.elmManager);

        Serial.println("[Display Init] HttpServerManager initialized");
    }

    void restore()
    {
        if (g_app.display)
            restoreDisplayImpl(*g_app.display, g_app.preferences);
    }
}
