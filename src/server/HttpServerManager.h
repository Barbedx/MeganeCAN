#pragma once

#include <PsychicHttp.h> 
#include "display/IDisplay.h"
#include <Preferences.h>

#include "commands/DisplayCommands.h" // Include the DisplayCommands manager    
 

class HttpServerManager {
public:
    HttpServerManager(IDisplay &display, Preferences &prefs);

    void begin();
    
private: 
    PsychicHttpServer _server;
    IDisplay &_display;
    Preferences &_prefs; 
    DisplayCommands::Manager _commands;

    void setupRoutes();
};
