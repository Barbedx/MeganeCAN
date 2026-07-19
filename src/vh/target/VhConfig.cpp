#include "VhConfig.h"
#include <Preferences.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace VhConfig
{
    bool     canboxEnabled = true;
    bool     keyForward = true;
    int16_t  rpmMul = 1;
    int16_t  rpmDiv = 1;
    uint32_t sigDisable = 0;
    uint8_t  capMode = 0;
    uint16_t capSecs = 60;

    static uint32_t s_gen = 0;
    uint32_t generation() { return s_gen; }

    static const char* NS = "vhcfg";

    void load()
    {
        Preferences p;
        p.begin(NS, /*readOnly=*/false);   // create the namespace on first boot
        canboxEnabled = p.getBool("canbox", true);
        keyForward    = p.getBool("keyfwd", true);
        rpmMul        = (int16_t)p.getShort("rpmmul", 1);
        rpmDiv        = (int16_t)p.getShort("rpmdiv", 1);
        if (rpmDiv == 0) rpmDiv = 1;
        sigDisable    = p.getUInt("sigdis", 0);
        capMode       = p.getUChar("capmode", 0);
        capSecs       = p.getUShort("capsecs", 60);
        p.end();
        s_gen++;
    }

    bool set(const char* key, const char* value)
    {
        if (!key || !value) return false;
        long v = strtol(value, nullptr, 0);

        Preferences p;
        p.begin(NS, false);
        bool ok = true;
        if      (!strcmp(key, "canbox"))  { canboxEnabled = v != 0; p.putBool("canbox", canboxEnabled); }
        else if (!strcmp(key, "keyfwd"))  { keyForward = v != 0;    p.putBool("keyfwd", keyForward); }
        else if (!strcmp(key, "rpmmul"))  { rpmMul = (int16_t)v;    p.putShort("rpmmul", rpmMul); }
        else if (!strcmp(key, "rpmdiv"))  { rpmDiv = (int16_t)(v ? v : 1); p.putShort("rpmdiv", rpmDiv); }
        else if (!strcmp(key, "sigdis"))  { sigDisable = (uint32_t)v; p.putUInt("sigdis", sigDisable); }
        else if (!strcmp(key, "capmode")) { capMode = (uint8_t)v;   p.putUChar("capmode", capMode); }
        else if (!strcmp(key, "capsecs")) { capSecs = (uint16_t)v;  p.putUShort("capsecs", capSecs); }
        else ok = false;
        p.end();
        if (ok) s_gen++;
        return ok;
    }

    bool get(const char* key, char* out, size_t outLen)
    {
        if (!key || !out || !outLen) return false;
        long v;
        if      (!strcmp(key, "canbox"))  v = canboxEnabled;
        else if (!strcmp(key, "keyfwd"))  v = keyForward;
        else if (!strcmp(key, "rpmmul"))  v = rpmMul;
        else if (!strcmp(key, "rpmdiv"))  v = rpmDiv;
        else if (!strcmp(key, "sigdis"))  v = (long)sigDisable;
        else if (!strcmp(key, "capmode")) v = capMode;
        else if (!strcmp(key, "capsecs")) v = capSecs;
        else return false;
        snprintf(out, outLen, "%ld", v);
        return true;
    }
}
