#pragma once
#include "Log.h"

// Back-compat shim. The chatty per-frame AFFA3 ISO-TP / sync narration ("Sending
// packet", "Waiting...", "PARTIAL ack", "DONE received", sync handshake ...) now flows
// through the leveled logger at DBG under category "AFFA". It is therefore silent at the
// default INF level (so continuous now-playing re-renders no longer flood the link) and
// is enabled at runtime with  /api/loglevel?n=3  (DBG) or higher.
//
// This replaces the old `volatile bool g_affaVerbose` + `vb` serial toggle: the bool is
// gone (folded into the log level) and the `vb` command now maps to Log::setLevel — see
// SerialConsole.cpp. (Serial INPUT is dead on the C3 USB-CDC anyway; use the HTTP knob.)
#define AFFA3_PRINT(...) LOGD("AFFA", __VA_ARGS__)
