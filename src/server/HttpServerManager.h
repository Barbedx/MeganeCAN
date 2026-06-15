#pragma once

#include <PsychicHttp.h> 
#include "display/IDisplay.h"
#include <Preferences.h>
#include <Arduino.h>

#include "commands/DisplayCommands.h" // Include the DisplayCommands manager    
#include <ElmManager/MyELMManager.h>
 

class WsWireLink;   // fwd: WebSocket WireProto link, registered during begin()

class HttpServerManager {
public:
    HttpServerManager(IDisplay &display, Preferences &prefs);

  void attachElm(MyELMManager* mgr) { elm = mgr; }   // <-- add this
  void attachWire(WsWireLink* link) { _wire = link; } // WebSocket frame stream

    void begin();

private:
  MyELMManager* elm = nullptr;
  WsWireLink* _wire = nullptr;
    PsychicHttpServer _server;
    IDisplay &_display;
    Preferences &_prefs; 
    DisplayCommands::Manager _commands;

    void setupRoutes();
};
