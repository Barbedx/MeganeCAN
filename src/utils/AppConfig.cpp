#include "AppConfig.h"
#include <Preferences.h>

namespace AppConfig
{
    String displayType = "carminat";
    String btMode      = "ams";
    bool   autoTime    = true;
    bool   elmEnabled  = false;
    bool   skipFuncReg = false;

    uint32_t schemaVersion = 0;
    bool     provisioned   = false;

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

        // First-run / migration. provisioned tracks whether the device has been set
        // up. A legacy device that already has a display_type but no flag is treated
        // as provisioned (migrated forward) so deployed units don't reset to setup.
        schemaVersion = prefs.getUInt("schema_ver", 0);
        if (prefs.isKey("provisioned")) {
            provisioned = prefs.getBool("provisioned", false);
        } else if (prefs.isKey("display_type")) {
            provisioned = true;
            prefs.putBool("provisioned", true);   // migrate legacy device
        } else {
            provisioned = false;                  // fresh flash -> needs first-run setup
        }
        if (schemaVersion != SCHEMA_VERSION) {
            prefs.putUInt("schema_ver", SCHEMA_VERSION);
            schemaVersion = SCHEMA_VERSION;
        }
        prefs.end();
    }

    DisplayKind displayKind()
    {
        if (displayType == "carminat")        return DisplayKind::Carminat;
        if (displayType == "updatelist")      return DisplayKind::UpdateListSeg;
        if (displayType == "updatelist_menu") return DisplayKind::UpdateListLcd;
        return DisplayKind::Unknown;
    }

    BtKind btKind() { return btMode == "keyboard" ? BtKind::Keyboard : BtKind::Ams; }

    const char* displayKindStr(DisplayKind k)
    {
        switch (k) {
            case DisplayKind::Carminat:      return "carminat";
            case DisplayKind::UpdateListSeg: return "updatelist";
            case DisplayKind::UpdateListLcd: return "updatelist_menu";
            default:                         return "unknown";
        }
    }

    bool isUnconfigured() { return !provisioned; }

    void markProvisioned()
    {
        Preferences prefs;
        prefs.begin("config", false);
        prefs.putBool("provisioned", true);
        prefs.end();
        provisioned = true;
    }
}
