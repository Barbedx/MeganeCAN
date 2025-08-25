#include "MyELMManager.h"
 
// ===== Queries per group =====
 
bool MyELMManager::connectTcpOnce(uint16_t port) {
    Serial.printf("[ELM] TCP connect %s:%u ...\n", host.toString().c_str(), port);
    wifiClient.stop();
    wifiClient.setTimeout(5000);
    if (!wifiClient.connect(host, port)) {
        Serial.println("[ELM] TCP connect failed");
        return false;
    }
    Serial.println("[ELM] TCP connected");
    return true;
}
 

bool MyELMManager::initElmOnce() {
  

// after (payload 256 is fine):
//elm.begin(wifiClient, true, 2000, ISO_15765_11_BIT_500_KBAUD, 256, 0);
// debug=false to stop the spammy "Received char" logs
// protocol='6' (ISO 15765 11-bit 500k) to avoid the library’s AUTO + 0100 probe
// payloadLen=256 (or 512), dataTimeout=400ms so lib sends a sane ST, not 00
elm.begin(wifiClient, true, 5000, ISO_15765_11_BIT_500_KBAUD, 256, 225);
Serial.println("Elm.begin done");
elm.sendCommand_Blocking("ATSH 7E0");
elm.sendCommand_Blocking("10 C0");

  // Init sequence (per ELM spec and my tests)
  // Some commands need a delay after them to work reliably
  // (especially ATZ, which does a full reset)
  // elm.sendCommand_Blocking("AT CAF1");      // CAN auto-format ON (safe with FC)
     
//elm.sendCommand_Blocking("ATE0");      // echo off (ELMduino already does, but be explicit)
//elm.sendCommand_Blocking("ATL0");      // no LF
//elm.sendCommand_Blocking("ATS0");      // no spaces
//elm.sendCommand_Blocking("ATH1");      // SHOW headers while debugging
//elm.sendCommand_Blocking("ATAL");      // allow long
//elm.sendCommand_Blocking("ATCAF1");    // auto-format
//elm.sendCommand_Blocking("ATCFC1");    // <-- auto flow control (old, widely supported)
//elm.sendCommand_Blocking("ATSP6");     // ISO15765-4, 11-bit, 500k
//elm.sendCommand_Blocking("ATST 96");   // ~600 ms timeout (0x96 * 4ms)

// Start with NO RX filter so we don't miss replies on a nearby ID
//elm.sendCommand_Blocking("ATCRA 000"); // disable response filter



// Engine addressing (11-bit)
//elm.sendCommand_Blocking("ATSH 7E0");

// Try a default diagnostic session first
//elm.sendCommand_Blocking("1081");      // default session
// Keep-alive is important; do this periodically in your loop (every ~2s)
//elm.sendCommand_Blocking("3E00");

// Now a simple read (will likely be multi-frame)
//elm.sendCommand_Blocking("21A0");      // expect 61 A0 ... with headers like "7E8 10 xx" then "7E8 21 ..."

// Engine ECU (11-bit example)
// If you truly have 11-bit on this ECU, do this before sending any PIDs:
 
// Try session:

 // (If that’s NODATA, try: elm.sendCommand_Blocking("10 C0");)

Serial.println("Elm init commands done");
    // Strict init; fail-fast if any step fails
    // if (!at("ATD"))      return false;  // defaults
    // if (!at("ATZ"))      return false;  // reset
    // if (!at("ATE0"))     return false;  // echo off
    // if (!at("ATS0"))     return false;  // no spaces
    // if (!at("ATL0"))     return false;  // no LF
    // if (!at("ATAL"))     return false;  // allow long
    // if (!at("ATST 64"))  return false;  // shorter timeout
    // if (!at("ATSP6"))    return false;  // ISO 15765-4 CAN (11/500k)
    Serial.println("Elm INITIALIZED!!!");
    nb_query_state   = SEND_COMMAND;
    currentGroup     = currentQuery = 0;
    lastQueryTime    = 0;
    waitingHeaderAck = false;
    elmReady         = true;
    lastGoodRxMs     = millis();
    tcpBackoffMs     = 1000;            // reset backoff on success
    return true;
}

bool MyELMManager::ensureTcpAndElm() {
    const uint32_t now = millis();

    // If TCP socket dropped → schedule reconnect
    if (!wifiClient.connected()) {
        if (now < nextTcpAttemptMs) return false;
        // try both ports (start with last good index)
        for (int i = 0; i < 2; ++i) { 
            if (connectTcpOnce(port)) {
                if (initElmOnce()) return true;
                // init failed → drop and try next/future
                disconnectTcp();
            }
        }
        // All attempts failed → backoff
        nextTcpAttemptMs = now + tcpBackoffMs;
        tcpBackoffMs = std::min<uint32_t>(tcpBackoffMs * 2, kBackoffMaxMs);
        Serial.printf("[ELM] Reconnect scheduled in %u ms\n", tcpBackoffMs);
        return false;
    }

    // TCP is up; if ELM isn't ready, try to init (no backoff here)
    if (!elmReady) {
        if (initElmOnce()) return true;
        disconnectTcp();
        nextTcpAttemptMs = now + tcpBackoffMs;
        tcpBackoffMs = std::min<uint32_t>(tcpBackoffMs * 2, kBackoffMaxMs);
        return false;
    }

    // LIVENESS: if no successful RX for too long, consider the link dead
    if ((now - lastGoodRxMs) > kDeadLinkMs) {
        Serial.println("[ELM] Link looks dead (no good RX). Reconnecting...");
        disconnectTcp();
        return false;
    }
    return true;
}
