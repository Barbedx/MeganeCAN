#include "MyELMManager.h"
using std::vector;

// ----------------- WIFI/TCP/ELM -----------------
bool MyELMManager::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.println("[ELM] WiFi not connected, reconnecting...");
  WiFi.reconnect(); // assumes creds already set elsewhere
  return false;
}

bool MyELMManager::connectTcpOnce(uint16_t p) {
  Serial.printf("[ELM] TCP connect %s:%u ...\n", host.toString().c_str(), p);
  wifiClient.stop();
  wifiClient.setTimeout(5000);
  if (!wifiClient.connect(host, p)) {
    Serial.println("[ELM] TCP connect failed");
    return false;
  }
  Serial.println("[ELM] TCP connected");
  return true;
}

void MyELMManager::disconnectTcp() {
  wifiClient.stop();
  elmReady = false;
  waiting = waitingHeaderOK = waitingSDS = waitingPing = waitingPid = false;
  currentHeader = "";
}

bool MyELMManager::initElmOnce() {
  // Lock protocol to ISO15765-4 (11bit, 500k)
  elm.begin(wifiClient, /*debug*/true, /*timeout*/5000,
            ISO_15765_11_BIT_500_KBAUD, /*rxBuf*/256, /*dataTimeout*/225);
  Serial.println("Elm.begin done");

  // Make sure protocol is locked (ELMduino often does TP, we enforce SP 6 too)
  elm.sendCommand_Blocking("AT SP 6");

  // Start with engine header + SDS (works for your S3000)
  elm.sendCommand_Blocking("ATSH 7E0");
  elm.sendCommand_Blocking("10 C0"); // expect 50 C0 (we won’t block here again)

  Serial.println("Elm init commands done");
  elmReady     = true;
  lastGoodRxMs = millis();
  lastCmdMs    = millis();
  tcpBackoffMs = 1000;
  sessions.clear();
  currentHeader = "7E0";       // we just set it
  sessions[currentHeader].open = true;     // we asked 10 C0 right away
  sessions[currentHeader].lastMs = millis();
  return true;
}

bool MyELMManager::ensureTcpAndElm() {
  const uint32_t now = millis();

  if (!ensureWifi()) return false;

  if (!wifiClient.connected()) {
    if (now < nextTcpAttemptMs) return false;
    if (connectTcpOnce(port) && initElmOnce()) return true;
    disconnectTcp();
    nextTcpAttemptMs = now + tcpBackoffMs;
    tcpBackoffMs = std::min<uint32_t>(tcpBackoffMs * 2, kBackoffMaxMs);
    Serial.printf("[ELM] Reconnect scheduled in %u ms\n", tcpBackoffMs);
    return false;
  }

  if (!elmReady) {
    if (initElmOnce()) return true;
    disconnectTcp();
    nextTcpAttemptMs = now + tcpBackoffMs;
    tcpBackoffMs = std::min<uint32_t>(tcpBackoffMs * 2, kBackoffMaxMs);
    return false;
  }

  if ((now - lastGoodRxMs) > kDeadLinkMs) {
    Serial.println("[ELM] Link looks dead (no good RX). Reconnecting...");
    disconnectTcp();
    return false;
  }
  return true;
}

// ----------------- helpers: header / SDS / ping -----------------
void MyELMManager::sendHeaderIfNeeded(const char* header) {
  if (currentHeader == header) return;
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "ATSH %s", header);
  elm.sendCommand(cmd);
  waiting = true; waitingHeaderOK = true;
  lastCmdMs = millis();
  // currentHeader will be set after we get OK
}

bool MyELMManager::openSessionIfNeeded(const char* header) {
  Sess& s = sessions[header]; // default-constructed if missing
  const uint32_t now = millis();

  // reopen if idle too long
  if (s.open && (now - s.lastMs) > kReopenSdsMs) {
    Serial.printf("[SDS] idle>=%ums -> re-open 10 C0 (%s)\n", kReopenSdsMs, header);
    s.open = false;
  }

  if (!s.open && !waiting) {
    elm.sendCommand("10 C0");
    waiting = true; waitingSDS = true;
    lastCmdMs = now;
    return false; // we just sent it; caller should wait
  }
  return s.open;
}

bool MyELMManager::sendTesterIfDue(const char* header) {
  Sess& s = sessions[header];
  const uint32_t now = millis();
  if ((now - s.lastMs) > kPingPeriodMs && !waiting) {
    elm.sendCommand("3E 00"); // expect 7F 3E 12 (OK)
    waiting = true; waitingPing = true;
    lastCmdMs = now;
    return true;
  }
  return false;
}

// ----------------- ASCII payload -> UDS bytes -----------------
static inline bool isHexChar(char c) {
  return (c>='0' && c<='9') || (c>='A' && c<='F') || (c>='a' && c<='f');
}
static inline uint8_t hexNibble(char c) {
  if (c>='0' && c<='9') return uint8_t(c - '0');
  if (c>='A' && c<='F') return uint8_t(10 + c - 'A');
  return uint8_t(10 + c - 'a');
}

std::vector<uint8_t> MyELMManager::decodeToUdsData(const char* ascii, size_t len) {
  // Build a string, split lines, keep only the part AFTER ':' on lines like "0:..."
  String s;
  s.reserve(len);
  for (size_t i=0;i<len;i++) s += char(ascii[i]);

  // concatenate data parts after ':' per line
  String hexJoined;
  int start = 0;
  while (start < (int)s.length()) {
    int end = s.indexOf('\r', start);
    if (end < 0) end = s.length();
    String line = s.substring(start, end);
    line.trim();
    int colon = line.indexOf(':');
    if (colon >= 0) {
      hexJoined += line.substring(colon+1);
    } else {
      // Lines that already are pure hex (e.g. single-frame) – keep if they look like data
      if (line.length() >= 4) hexJoined += line;
    }
    start = end + 1;
  }
  // strip spaces
  String noSpace;
  noSpace.reserve(hexJoined.length());
  for (size_t i=0;i<(size_t)hexJoined.length();++i) {
    char c = hexJoined[i];
    if (c!=' ' && c!='\t') noSpace += c;
  }

  // Convert hex pairs
  vector<uint8_t> raw;
  int i = 0;
  while (i+1 < noSpace.length()) {
    if (isHexChar(noSpace[i]) && isHexChar(noSpace[i+1])) {
      uint8_t b = (hexNibble(noSpace[i])<<4) | hexNibble(noSpace[i+1]);
      raw.push_back(b);
      i += 2;
    } else {
      i += 1;
    }
  }

  // Strip UDS service+PID if present (e.g., 61 A0 ...)
  return diag::udsDataOnly(raw);
}

// ----------------- main state machine -----------------
void MyELMManager::tick() {
  const uint32_t now = millis();

  // 0) ensure connectivity and ELM
  if (!ensureTcpAndElm()) return;
  if (plan.empty()) return;

  // Enforce a minimum spacing between commands
  if (waiting || (now - lastCmdMs) < kCmdIntervalMs) {
    // poll for response if waiting
    if (waiting) {
      elm.get_response();
      if (elm.nb_rx_state == ELM_SUCCESS) {
        lastGoodRxMs = now;

        if (waitingHeaderOK) {
          waitingHeaderOK = false; waiting = false;
          currentHeader = ""; // will set to target below on next loop
          // We consider 'OK' received; nothing else to do here
          return;
        }
        if (waitingSDS) {
          waitingSDS = false; waiting = false;
          // check 50 C0
          if (elm.PAYLOAD_LEN >= 2 && elm.payload[0] == 0x35 && elm.payload[1] == 0x30) {
            // payload was ASCII "50..." → decode first
            auto uds = decodeToUdsData(elm.payload, elm.PAYLOAD_LEN);
            if (uds.size() >= 2 && uds[0]==0x50 && uds[1]==0xC0) {
              sessions[currentHeader].open = true;
              sessions[currentHeader].lastMs = now;
            }
          } else if (elm.PAYLOAD_LEN >= 2 && elm.payload[0]==0x50 && elm.payload[1]==0xC0) {
            sessions[currentHeader].open = true;
            sessions[currentHeader].lastMs = now;
          } else {
            // tolerate variants; mark open anyway since your logs show it works
            sessions[currentHeader].open = true;
            sessions[currentHeader].lastMs = now;
          }
          return;
        }
        if (waitingPing) {
          waitingPing = false; waiting = false;
          // accept 7F 3E 12 or anything; refresh lastMs
          sessions[currentHeader].lastMs = now;
          return;
        }
        if (waitingPid) {
          waitingPid = false; waiting = false;

          // Convert ASCII → UDS data bytes (w/o 61 xx)
          auto data = decodeToUdsData(elm.payload, elm.PAYLOAD_LEN);

          // Evaluate all metrics in this plan node
          const auto& node = plan[planIndex];
          for (const auto& m : node.metrics) {
            float v = 0.0f;
            if (m.eval) {
              v = m.eval(data);
              valueCache[String(m.shortName)] = v;
            }
          }

          // mark activity on this header
          sessions[currentHeader].lastMs = now;

          // advance to next PID
          planIndex = (planIndex + 1) % plan.size();
          return;
        }

        // generic success fallback
        waiting = false;
        return;
      }
      else if (elm.nb_rx_state != ELM_GETTING_MSG) {
        // error: drop waiting and advance to next PID
        Serial.print("[ELM] error state="); Serial.println(elm.nb_rx_state);
        waiting = waitingHeaderOK = waitingSDS = waitingPing = waitingPid = false;
        planIndex = (planIndex + 1) % plan.size();
        // don't spam; small backoff comes from kCmdIntervalMs
        return;
      }
    }
    return;
  }

  // 1) choose current plan node
  const auto& node = plan[planIndex];

  // 2) header change?
  if (currentHeader != node.header) {
    currentHeader = node.header; // set target; will be confirmed after OK
    sendHeaderIfNeeded(node.header);
    return;
  }

  // 3) SDS (10 C0) if needed
  if (node.needsSession) {
    if (!openSessionIfNeeded(node.header)) return; // just sent 10 C0
  }

  // 4) Tester Present if due (only when idle)
  if (sendTesterIfDue(node.header)) return;

  // 5) send the PID
  elm.sendCommand(node.modePid);
  waiting = true; waitingPid = true;
  lastCmdMs = now;
}
