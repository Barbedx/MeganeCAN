#pragma once
#include <Arduino.h>

// Lightweight in-RAM ring-buffer logger. Log::printf() mirrors to Serial AND
// keeps the last ~16 KB of lines (with a millis() timestamp) so they can be
// downloaded over WiFi at /api/log — handy for debugging in the car where the
// serial port isn't connected. Buffer lives in RAM (lost on reboot/crash; the
// firmware also has a coredump partition for crash post-mortems).
namespace Log {
    void   printf(const char *fmt, ...);
    String dump();
    void   clear();
}
