#include "Log.h"

#include <stdarg.h>

// The RAM ring log was removed: under load it wrapped within seconds (so it held
// almost no useful history) yet cost ~16KB of static RAM plus a same-size
// transient std::string on every /api/log read, which fragmented a memory-tight
// ESP32 and helped wedge the web server. Live debugging now uses the serial
// console / the serial proxy (tools/serial_proxy.py). Log::printf still mirrors
// to Serial so all call sites keep working unchanged.
namespace Log
{
    void printf(const char *fmt, ...)
    {
        char line[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line, sizeof(line), fmt, ap);
        va_end(ap);
        Serial.printf("[%lu] %s\n", (unsigned long)millis(), line);
    }

    String dump() { return String("RAM log disabled — use the serial console / proxy."); }

    void clear() {}
}
