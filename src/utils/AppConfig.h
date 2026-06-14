#pragma once
#include <Arduino.h>

// Config cached in RAM, read from NVS once at boot. The dashboard config getters
// previously opened NVS ("config", read-only) on every request, which both churned
// the heap and spammed "nvs_open failed: NOT_FOUND" when the namespace did not exist
// yet (fresh flash). Load() creates the namespace so those errors stop, and the
// getters now read these RAM fields instead of touching NVS.
namespace AppConfig
{
    extern String displayType; // "carminat" | "updatelist" | "updatelist_menu"
    extern String btMode;      // "ams" | "keyboard"
    extern bool   autoTime;
    extern bool   elmEnabled;
    extern bool   skipFuncReg;

    void Load(); // read NVS "config" into RAM once (call at boot)
}
