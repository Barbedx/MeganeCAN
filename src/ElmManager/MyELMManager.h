#pragma once

// #include <PsychicHttp.h>
#include "display/IDisplay.h"
// #include <Preferences.h>
#include "ELMduino.h"
#include "commands/DisplayCommands.h" // Include the DisplayCommands manager
#include "WiFi.h"
class MyELMManager
{
public:
    MyELMManager(IDisplay &display);
    void setup()
    {
        elm.begin(wifiClient, true, 2000);
    }

    void tick()
    {
        //  static int queryState = SEND_COMMAND;
        static unsigned long lastQueryTime = 0;
        const long queryInterval = 500;
    };

private:
    IDisplay &display;
    ELM327 elm;
    WiFiClient wifiClient;

    using EvalFunc = std::function<float(int, int)>;
    // add callback binding to command?
    struct QueryData
    {
        const char *modeAndPid;
        const char *name;
        EvalFunc evalFunc;
        /* data */
    };
    static const QueryData queryList[]; // only declare
};