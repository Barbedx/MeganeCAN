#pragma once
#include <Arduino.h>
#include <map>
#include "Menu/Menu.h"

class CarminatDisplay;   // DiagPage renders through the concrete display (showMenu + popPage)
class DiagPage;
class MyELMManager;

// Owns the OBD/ELM diagnostics concern: the per-ECU DiagPage objects and the live
// menu fields fed from ELM updates. Cut verbatim from CarminatDisplay — the
// coordinator's attachElm/onElmUpdate delegate here, and initializeMenu looks pages
// up via page() to push them on activation.
//
// DiagPage still takes CarminatDisplay& (it renders via the display), so this holds
// the display by reference; it holds the menu by reference for the live-value
// updates (Voltage/Boost). ELM is off by default, so this path is low-risk.
class DiagController {
public:
    DiagController(CarminatDisplay& display, Menu& menu) : _display(display), _menu(menu) {}
    ~DiagController();

    void attachElm(MyELMManager* m);                 // build the per-ECU pages
    void onElmUpdate(const char* key, float value);  // live menu fields from ELM
    DiagPage* page(const String& header);            // lookup for menu activation

private:
    CarminatDisplay&            _display;
    Menu&                       _menu;
    MyELMManager*               _elm = nullptr;
    std::map<String, DiagPage*> _diagPages;
};
