#pragma once
#include <stdint.h>
#include <stddef.h>

// VH board NVS config (ARCHITECTURE-V2 §5). Everything the sniff/bring-up
// campaign might want to tweak is a config, not a rebuild — settable over the
// link (CFG_GET/SET from MM's web UI) with no reflashing trips. String-keyed
// so the tunnel stays generic; values parse as integers (bools are 0/1).
namespace VhConfig
{
    // RAM copies (loaded once at boot, updated by set()).
    extern bool     canboxEnabled;   // "canbox"  — Raise emitter on/off
    extern bool     keyForward;      // "keyfwd"  — forward MM KEY_EVT to the HU
    extern int16_t  rpmMul;          // "rpmmul"  — RPM scale (§8.4: verify on DUDU)
    extern int16_t  rpmDiv;          // "rpmdiv"
    extern uint32_t sigDisable;      // "sigdis"  — VehicleDecoder per-signal mask
    extern uint8_t  capMode;         // "capmode" — default capture mode
    extern uint16_t capSecs;         // "capsecs" — default capture duration

    void load();

    // Generic access for the CFG tunnel. Unknown key -> false.
    bool set(const char* key, const char* value);   // persists + updates RAM
    bool get(const char* key, char* out, size_t outLen);

    // Bumped by set(); the GW main re-applies runtime knobs when it changes.
    uint32_t generation();
}

#include "LinkTunnel.h"   // ITunnelConfig

// The GW's tunnel-config binding: exposes the vehicle/canbox knobs above.
struct VhConfigAdapter : ITunnelConfig {
    bool get(const char* key, char* out, size_t outLen) override
    { return VhConfig::get(key, out, outLen); }
    bool set(const char* key, const char* value) override
    { return VhConfig::set(key, value); }
};
