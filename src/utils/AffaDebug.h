#pragma once
#include <cstdio>
#include <cstdarg>

// Runtime gate for the chatty per-frame AFFA3 ISO-TP narration ("Sending packet",
// "Waiting...", "PARTIAL ack", "DONE received" ...). OFF by default: during a
// continuous now-playing re-render each screen is a ~12-frame ISO-TP burst and this
// narration alone is 3+ lines per frame — it floods the 115200 USB-CDC link faster
// than the PC drains it, the CDC TX buffer wedges and serial output stalls (the
// "serial stops after connecting to CAN" symptom). The [TX]/[RX] CAN frame dumps and
// the @TX wire lines are NOT gated here, so you can still monitor the bus. Flip it at
// runtime over serial: `vb 1` (verbose on) / `vb 0` (off). Defined in SerialConsole.cpp.
extern volatile bool g_affaVerbose;

inline void AFFA3_PRINT(const char *fmt, ...)
{
  if (!g_affaVerbose) return;
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}
