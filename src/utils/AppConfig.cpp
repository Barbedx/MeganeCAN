#include "AppConfig.h"
#include <Preferences.h>

namespace AppConfig
{
    String displayType = "carminat";
    String btMode      = "ams";
    bool   autoTime    = true;
    bool   elmEnabled  = false;
    bool   skipFuncReg = false;

    void Load()
    {
        Preferences prefs;
        // Open read-WRITE so the "config" namespace is created if missing — this is
        // what stops the later read-only opens from logging NOT_FOUND on a fresh flash.
        prefs.begin("config", /*readOnly=*/false);
        displayType = prefs.getString("display_type", "carminat");
        btMode      = prefs.getString("bt_mode",      "ams");
        autoTime    = prefs.getBool("auto_time",      true);
        elmEnabled  = prefs.getBool("elm_enabled",    false);
        skipFuncReg = prefs.getBool("skip_funcreg",   false);
        prefs.end();
    }
}
