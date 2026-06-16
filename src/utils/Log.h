#pragma once

// In the native (host) test build there is no Arduino / WireProto / Log.cpp. Provide
// header-only no-op stubs so the portable logic layer (which pulls AffaDebug.h ->
// Log.h via AffaDisplayBase) compiles and links without the firmware logger.
#ifdef NATIVE
#include <cstdint>
enum class LogLevel : uint8_t { ERR = 0, WRN = 1, INF = 2, DBG = 3, TRC = 4 };
namespace Log {
    inline void        setLevel(LogLevel) {}
    inline LogLevel    level() { return LogLevel::INF; }
    inline bool        enabled(LogLevel) { return false; }
    inline const char *levelName(LogLevel) { return ""; }
    inline void        emit(LogLevel, const char *, const char *, ...) {}
    inline void        printf(const char *, ...) {}
}
#define LOGE(cat, ...) ((void)0)
#define LOGW(cat, ...) ((void)0)
#define LOGI(cat, ...) ((void)0)
#define LOGD(cat, ...) ((void)0)
#define LOGT(cat, ...) ((void)0)
#else // ---- firmware build ------------------------------------------------------
#include <Arduino.h>

// ============================================================================
// Leveled, categorized logger — the single human-log seam for the firmware.
//
// Lines are routed through WireProto::emitLine() so they reach BOTH the USB serial
// (pio monitor / serial proxy) AND the /wire WebSocket, where they can be filtered
// by level and category. Output format is one record per line:
//
//     <L> [<cat>] <message>          e.g.  "I [BT] phone connected"
//                                          "E [ELM] TCP connect failed"
//                                          "T [CAN] RX 151 21 20 20 ..."
//
//   L = single severity char: E(rror) W(arn) I(nfo) D(ebug) T(race).
//
// Severity guide:
//   ERR  a failure (connect failed, timeout, protocol error)
//   WRN  unexpected-but-handled (unknown packet ignored, no live bus)
//   INF  state transitions / user-visible actions (connected, mode change, init)
//   DBG  per-operation narration (ISO-TP send steps, menu navigation)
//   TRC  per-frame firehose (every CAN [RX]/[TX] human dump) — off unless chasing wire
//
// Gated by a runtime threshold (default INF) settable over HTTP at /api/loglevel?n=<0..4>
// — HTTP because serial INPUT is dead on the C3 USB-CDC. A message is emitted only when
// its level <= threshold; when gated out, emit() returns before formatting, so the
// printf cost is paid only for lines that are actually shown (cheap in hot paths). For
// call sites that must build a payload (e.g. a hex dump) before logging, guard the build
// with Log::enabled(LogLevel::TRC) first.
//
// Fixed stack buffer, no String — safe in the CAN callback / ISO-TP send path.
// The machine @TX/@RX CAN frames are a SEPARATE channel (WireProto::emitTx) and are
// unaffected by the log level.
// ============================================================================
enum class LogLevel : uint8_t { ERR = 0, WRN = 1, INF = 2, DBG = 3, TRC = 4 };

namespace Log {
    void     setLevel(LogLevel lvl);
    LogLevel level();
    bool     enabled(LogLevel lvl);   // true when lvl <= current threshold
    const char *levelName(LogLevel lvl);

    void emit(LogLevel lvl, const char *cat, const char *fmt, ...)
        __attribute__((format(printf, 3, 4)));

    // --- back-compat shims (pre-existing API; keep old call sites compiling) -------
    // printf() now maps to INF with no category; dump()/clear() are no-ops (the RAM
    // ring log was removed long ago — see /wire + pio monitor).
    void   printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    String dump();
    void   clear();
}

// Category is a short string literal: "BT", "ELM", "CAN", "AFFA", "DISP", "WIFI",
// "MENU", "HTTP", "SYS", ... Keep them short and stable (they are filter keys).
#define LOGE(cat, ...) Log::emit(LogLevel::ERR, cat, __VA_ARGS__)
#define LOGW(cat, ...) Log::emit(LogLevel::WRN, cat, __VA_ARGS__)
#define LOGI(cat, ...) Log::emit(LogLevel::INF, cat, __VA_ARGS__)
#define LOGD(cat, ...) Log::emit(LogLevel::DBG, cat, __VA_ARGS__)
#define LOGT(cat, ...) Log::emit(LogLevel::TRC, cat, __VA_ARGS__)
#endif // NATIVE
