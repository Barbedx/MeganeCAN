#pragma once

#include <PsychicHttp.h>
#include "display/Affa3/Affa3Display.h"
#include "display/Affa3Nav/Affa3NavDisplay.h"
#include <Preferences.h>
Preferences preferences;

class HttpServerManager {
public:
    HttpServerManager(PsychicHttpServer& server, Preferences& prefs, Affa3NavDisplay& display);

    void begin();
    
private:
    PsychicHttpServer& _server;
    Preferences& _prefs;
    Affa3NavDisplay& _display;

    void setupRoutes();
};
