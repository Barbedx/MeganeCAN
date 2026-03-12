#pragma once

#include <Preferences.h>

#include "display/AffaDisplayBase.h"
#include "ElmManager/MyELMManager.h"
// Forward declarations to avoid heavy includes in header
class AffaDisplayBase;
class HttpServerManager;
class MyELMManager;

struct AppContext {
    AffaDisplayBase* display = nullptr;
    HttpServerManager* serverManager = nullptr;
    MyELMManager* elmManager = nullptr;

    Preferences preferences;

    bool autoTime = true;
    bool timeSyncDone = false;
    bool elmEnabled = false;

    unsigned long lastPingTime = 0;
};
