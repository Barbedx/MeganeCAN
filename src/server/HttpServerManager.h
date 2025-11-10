#pragma once

#include <PsychicHttp.h> 
#include "display/IDisplay.h"
#include <Preferences.h>

#include "commands/DisplayCommands.h" // Include the DisplayCommands manager    
#include <ElmManager/MyELMManager.h>
 

class HttpServerManager {
public:
    HttpServerManager(IDisplay &display, Preferences &prefs);

  void attachElm(MyELMManager* mgr) { elm = mgr; }   // <-- add this
  
    void begin();
    
private: 
  MyELMManager* elm = nullptr;   
    PsychicHttpServer _server;
    IDisplay &_display;
    Preferences &_prefs; 
    DisplayCommands::Manager _commands;

    void setupRoutes();
};
