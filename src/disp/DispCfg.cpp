#include "DispCfg.h"
#include "../utils/AppConfig.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

bool DispCfg::get(const char* key, char* out, size_t outLen)
{
    if (!key || !out || !outLen) return false;
    if (!strcmp(key, "display_type"))
    {
        strncpy(out, AppConfig::displayType.c_str(), outLen - 1);
        out[outLen - 1] = 0;
        return true;
    }
    if (!strcmp(key, "skip_funcreg"))
    {
        snprintf(out, outLen, "%d", AppConfig::skipFuncReg ? 1 : 0);
        return true;
    }
    if (!strcmp(key, "autorestore"))
    {
        Preferences p;
        p.begin("display", true);
        snprintf(out, outLen, "%d", p.getBool("autoRestore", false) ? 1 : 0);
        p.end();
        return true;
    }
    return false;
}

bool DispCfg::set(const char* key, const char* value)
{
    if (!key || !value) return false;

    if (!strcmp(key, "display_type"))
    {
        if (strcmp(value, "carminat") && strcmp(value, "updatelist") &&
            strcmp(value, "updatelist_menu"))
            return false;
        Preferences p;
        p.begin("config", false);
        p.putString("display_type", value);
        p.end();
        _rebootAtMs = millis() + 500;   // new driver needs a clean construction
        return true;
    }
    if (!strcmp(key, "skip_funcreg"))
    {
        Preferences p;
        p.begin("config", false);
        p.putBool("skip_funcreg", strtol(value, nullptr, 0) != 0);
        p.end();
        _rebootAtMs = millis() + 500;
        return true;
    }
    if (!strcmp(key, "autorestore"))
    {
        Preferences p;
        p.begin("display", false);
        p.putBool("autoRestore", strtol(value, nullptr, 0) != 0);
        p.end();
        return true;   // read at next boot; no reboot needed now
    }
    return false;
}
