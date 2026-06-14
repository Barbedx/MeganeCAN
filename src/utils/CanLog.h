#pragma once
#include <Arduino.h>

// Configurable CAN logger. onFrame() is fed every received CAN frame from the
// bus callback. It always tracks which IDs are seen (+ counts) so the web UI
// can show what's on the bus; when enabled it records matching frames (filtered
// by an ID allow-list, empty = all) into a RAM ring buffer downloadable at
// /api/can/log. Config (enabled + filter IDs) persists in NVS.
namespace CanLog {
    void   begin();   // load config from NVS
    void   onFrame(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data);
    String dump();    // recorded frames (text)
    void   clear();   // clear buffer + seen-id counters
    void   setConfig(bool enabled, const String &idsCsv); // persist enable + hex-ID allow-list
    String configJson(); // {"enabled":bool,"filter":"151,3CF","seen":[{"id":"151","n":N}...]}
}
