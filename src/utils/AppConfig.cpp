#include "AppConfig.h"
#include <Preferences.h>
#include "../keys/KeyRouter.h"   // defaultsForBtMode (key_sinks migration default)

namespace AppConfig
{
    String displayType = "carminat";
    String btMode      = "ams";
    bool   autoTime    = true;
    bool   elmEnabled  = false;
    bool   skipFuncReg = false;

    String  mediaSource = "ams";
    uint8_t keySinks    = 0;
    bool    linkEnabled = true;

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

        // v2 keys. Defaults preserve v1 behavior exactly: media from AMS, key
        // routing per bt_mode, link on (it no-ops until the VH board exists).
        mediaSource = prefs.getString("media_source", "ams");
        linkEnabled = prefs.getBool("link_enabled", true);
        if (prefs.isKey("key_sinks"))
            keySinks = prefs.getUChar("key_sinks", 0);
        else  // unset -> follow bt_mode, recomputed each boot (not persisted)
            keySinks = KeyRouter::defaultsForBtMode(btMode.c_str());

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

    BtKind btKind()
    {
        if (btMode == "keyboard") return BtKind::Keyboard;
        if (btMode == "both")     return BtKind::Both;
        return BtKind::Ams;
    }

    const char* btKindStr(BtKind k)
    {
        switch (k) {
            case BtKind::Keyboard: return "keyboard";
            case BtKind::Both:     return "both";
            default:               return "ams";
        }
    }

    bool isBtMode(const char* s)
    {
        if (!s) return false;
        return !strcmp(s, "ams") || !strcmp(s, "keyboard") || !strcmp(s, "both");
    }

    MediaSourceKind mediaSourceKind()
    {
        if (mediaSource == "hu")   return MediaSourceKind::Hu;
        if (mediaSource == "auto") return MediaSourceKind::Auto;
        return MediaSourceKind::Ams;
    }

    bool isMediaSource(const char* s)
    {
        if (!s) return false;
        return !strcmp(s, "ams") || !strcmp(s, "hu") || !strcmp(s, "auto");
    }

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
