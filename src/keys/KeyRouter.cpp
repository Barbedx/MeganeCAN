#include "KeyRouter.h"
#include <string.h>

uint8_t KeyRouter::pack(uint8_t transportMask, uint8_t volumeMask, bool volumeHoldOnly)
{
    return (uint8_t)((transportMask & SINK_MASK)
                   | ((volumeMask & SINK_MASK) << 4)
                   | (volumeHoldOnly ? HOLD_ONLY_BIT : 0));
}

void KeyRouter::unpack(uint8_t packed, uint8_t& transportMask, uint8_t& volumeMask,
                       bool& volumeHoldOnly)
{
    transportMask  = packed & SINK_MASK;
    volumeMask     = (packed >> 4) & SINK_MASK;
    volumeHoldOnly = (packed & HOLD_ONLY_BIT) != 0;
}

uint8_t KeyRouter::defaultsForBtMode(const char* btMode)
{
    if (btMode && !strcmp(btMode, "keyboard"))
        return pack(SINK_HID, SINK_HID, true);
    if (btMode && !strcmp(btMode, "both"))
        return pack(SINK_AMS, SINK_HID, false);
    return pack(SINK_AMS, SINK_AMS, true);   // "ams" (and anything unknown)
}

void KeyRouter::bind(uint8_t sinkBit, IKeySink* sink)
{
    for (int i = 0; i < 3; i++)
        if (sinkBit == (1u << i))
            _sinks[i] = sink;
}

void KeyRouter::configure(uint8_t packed)
{
    _packed = packed;
    unpack(packed, _transportMask, _volumeMask, _volumeHoldOnly);
}

bool KeyRouter::route(AffaCommon::AffaKey key, bool isHold)
{
    const bool vol = isVolumeKey(key);
    if (vol && _volumeHoldOnly)
    {
        // Legacy SWC semantics ("ams"/"keyboard" modes): volume acts on hold only,
        // and VolumeDown is never forwarded (the radio's own rocker handles it).
        if (key == AffaCommon::AffaKey::VolumeDown || !isHold)
            return true;
    }
    const uint8_t mask = vol ? _volumeMask : _transportMask;
    for (int i = 0; i < 3; i++)
        if ((mask & (1u << i)) && _sinks[i])
            _sinks[i]->onKey(key, isHold);
    return true;
}
