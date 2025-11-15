#include "MyELMManager.h"
using std::vector;

// ----------------- WIFI/TCP/ELM -----------------
bool MyELMManager::ensureWifi()
{
    if (WiFi.status() == WL_CONNECTED)
        return true;
    Serial.println("[ELM] WiFi not connected, reconnecting...");
    WiFi.reconnect(); // assumes creds already set elsewhere
    return false;
}

bool MyELMManager::connectTcpOnce(uint16_t p)
{
    Serial.printf("[ELM] TCP connect %s:%u ...\n", host.toString().c_str(), p);
    wifiClient.stop();
    wifiClient.setTimeout(5000);
    if (!wifiClient.connect(host, p))
    {
        Serial.println("[ELM] TCP connect failed");
        return false;
    }
    Serial.println("[ELM] TCP connected");
    return true;
}

void MyELMManager::disconnectTcp()
{
    wifiClient.stop();
    elmReady = false;
    waiting = waitingHeaderOK = waitingSDS = waitingPing = waitingPid = false;
    currentHeader = "";
}

bool MyELMManager::initElmOnce()
{
    // Lock protocol to ISO15765-4 (11bit, 500k)
    elm.begin(wifiClient, /*debug*/ true, /*timeout*/ 5000,
              ISO_15765_11_BIT_500_KBAUD, /*rxBuf*/ 256, /*dataTimeout*/ 225);
    Serial.println("Elm.begin done");

    // Make sure protocol is locked (ELMduino often does TP, we enforce SP 6 too)
    // elm.sendCommand_Blocking("AT SP 6");

    // Start with engine header + SDS (works for your S3000)
    //elm.sendCommand_Blocking("ATSH 7E0");
    //elm.sendCommand_Blocking("10C0"); // expect 50 C0 (we won’t block here again)
    //elm.sendCommand_Blocking("3E00"); // expect 50 C0 (we won’t block here again)
    
    
    // elm.sendCommand_Blocking("AT CRA 7E8");
    // elm.sendCommand_Blocking("AT FCSH 7E0");
    elm.sendCommand_Blocking("AT FCSD 300000");
    elm.sendCommand_Blocking("AT FC SM 0");
    elm.sendCommand_Blocking("AT SP 6");
    delay(10);
    elm.sendCommand_Blocking("AT SH 7E0");
    delay(3);
    elm.sendCommand_Blocking("10C0");

    // Make sure protocol is locked (ELMduino often does TP, we enforce SP 6 too)

    
    elmReady = true;
    nb_query_state = SEND_COMMAND;
    currentQuery = 0;
    lastQueryTime = 0;
    lastGoodRxMs = millis();
    tcpBackoffMs = 1000;
    Serial.println("Elm init commands done");

    waiting = waitingHeaderOK = waitingSDS = waitingPing = waitingPid = false;

    currentHeader = ""; // we just set it
    sessions.clear();
    return true;
}
String pendingHeader; // header we just asked ATSH for; commit on OK

bool MyELMManager::ensureTcpAndElm()
{
    const uint32_t now = millis();

    if (!ensureWifi())
        return false;

    if (!wifiClient.connected())
    {
        if (now < nextTcpAttemptMs)
            return false;
        if (connectTcpOnce(port) && initElmOnce())
            return true;
        disconnectTcp();
        nextTcpAttemptMs = now + tcpBackoffMs;
        tcpBackoffMs = std::min<uint32_t>(tcpBackoffMs * 2, kBackoffMaxMs);
        Serial.printf("[ELM] Reconnect scheduled in %u ms\n", tcpBackoffMs);
        return false;
    }

    if (!elmReady)
    {
        if (initElmOnce())
            return true;
        disconnectTcp();
        nextTcpAttemptMs = now + tcpBackoffMs;
        tcpBackoffMs = std::min<uint32_t>(tcpBackoffMs * 2, kBackoffMaxMs);
        return false;
    }

    if ((now - lastGoodRxMs) > kDeadLinkMs)
    {
        Serial.println("[ELM] Link looks dead (no good RX). Reconnecting...");
        disconnectTcp();
        return false;
    }
    return true;
}

// ----------------- helpers: header / SDS / ping -----------------
void MyELMManager::sendHeaderIfNeeded(const char *header)
{
    if (currentHeader == header)
    {
        Serial.println("Header already set, no need to send ATSH");
        return;
    }
    pendingHeader = header;
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "ATSH %s", header);

    Serial.printf("[SEND] %s\n", cmd);
    elm.sendCommand(cmd);
    waiting = true;
    waitingHeaderOK = true;
    lastCmdMs = millis();
    // currentHeader will be set after we get OK
}

bool MyELMManager::openSessionIfNeeded(const char *header)
{
    Sess &s = sessions[header]; // default-constructed if missing
    const uint32_t now = millis();

    // reopen if idle too long
    if (s.open && (now - s.lastMs) > kReopenSdsMs)
    {
        Serial.printf("[SDS] idle>=%ums -> re-open 10C0 (%s)\n", kReopenSdsMs, header);
        s.open = false;
    }

    if (!s.open && !waiting)
    {
         
        elm.sendCommand("10C0");
        waiting = true;
        waitingSDS = true;
        lastCmdMs = now;
        return false; // we just sent it; caller should wait
    }
    return s.open;
}

bool MyELMManager::sendTesterIfDue(const char *header)
{
    Sess &s = sessions[header];
    const uint32_t now = millis();
    if ((now - s.lastMs) > kPingPeriodMs && !waiting)
    {
        elm.sendCommand("3E00"); // expect 7F 3E 12 (OK)
        waiting = true;
        waitingPing = true;
        lastCmdMs = now;
        return true;
    }
    return false;
}

// ----------------- ASCII payload -> UDS bytes -----------------
static inline bool isHexChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
static inline uint8_t hexNibble(char c)
{
    if (c >= '0' && c <= '9')
        return uint8_t(c - '0');
    if (c >= 'A' && c <= 'F')
        return uint8_t(10 + c - 'A');
    return uint8_t(10 + c - 'a');
}

std::vector<uint8_t> MyELMManager::decodeToUdsData(const char *ascii, size_t len)
{
    // Build a string, split lines, keep only the part AFTER ':' on lines like "0:..."
    String s;
    s.reserve(len);
    for (size_t i = 0; i < len; i++)
        s += char(ascii[i]);

    // concatenate data parts after ':' per line
    String hexJoined;
    int start = 0;
    while (start < (int)s.length())
    {
        int end = s.indexOf('\r', start);
        if (end < 0)
            end = s.length();
        String line = s.substring(start, end);
        line.trim();
        int colon = line.indexOf(':');
        if (colon >= 0)
        {
            hexJoined += line.substring(colon + 1);
        }
        else
        {
            // Lines that already are pure hex (e.g. single-frame) – keep if they look like data
            if (line.length() >= 4)
                hexJoined += line;
        }
        start = end + 1;
    }
    // strip spaces
    String noSpace;
    noSpace.reserve(hexJoined.length());
    for (size_t i = 0; i < (size_t)hexJoined.length(); ++i)
    {
        char c = hexJoined[i];
        if (c != ' ' && c != '\t')
            noSpace += c;
    }

    // Convert hex pairs
    vector<uint8_t> raw;
    int i = 0;
    while (i + 1 < noSpace.length())
    {
        if (isHexChar(noSpace[i]) && isHexChar(noSpace[i + 1]))
        {
            uint8_t b = (hexNibble(noSpace[i]) << 4) | hexNibble(noSpace[i + 1]);
            raw.push_back(b);
            i += 2;
        }
        else
        {
            i += 1;
        }
    }

    // Strip UDS service+PID if present (e.g., 61 A0 ...)
    return udsDataOnly(raw);
}
// ---- debug print switch ----
#ifndef LOG_PID_VALUES
#define LOG_PID_VALUES 1 // set to 0 to silence value logs
#endif

static inline void dumpHex(const std::vector<uint8_t> &v)
{
#if LOG_PID_VALUES
    Serial.print("[DATA] ");
    for (uint8_t b : v)
        Serial.printf("%02X ", b);
    Serial.printf("(%u bytes)\n", (unsigned)v.size());
#endif
}
// ----------------- main state machine -----------------
void MyELMManager::tick()
{
    // Bring up TCP + ELM init with backoff; no-ops if already good
    // 0) ensure connectivity and ELM
    if (!ensureTcpAndElm())
    {
        Serial.println("ensureTcpAndElm failed");
        return;
    }
    if (plan.empty())
    {
        Serial.println("plan is empty");
        return;
    }

    const uint32_t now = millis();
    // Enforce a minimum spacing between commands
    if (waiting)
    {
        // Serial.println("waiting for response ...");
        elm.get_response();

        if (elm.nb_rx_state == ELM_GETTING_MSG)
        {
          //  lastGoodRxMs = now;
            return; // check if ping needed?
        }

        if (elm.nb_rx_state == ELM_SUCCESS)
        {
            Serial.println("got response ...");
            lastGoodRxMs = now;
            if (waitingHeaderOK)
            {
                waitingHeaderOK = false;
                waiting = false;
                currentHeader = pendingHeader; // commit to new header
                pendingHeader = "";
                // We consider 'OK' received; nothing else to do here
                Serial.printf("[RECV] OK for ATSH %s\n", currentHeader.c_str());
                return;
            }
            if (waitingSDS)
            {
                waitingSDS = false;
                waiting = false;
                auto uds = decodeToUdsData(elm.payload, elm.PAYLOAD_LEN);
                bool ok = uds.size() >= 2 && uds[0] == 0x50 && uds[1] == 0xC0;
                sessions[currentHeader].open = ok; // open if positive
                sessions[currentHeader].lastMs = now;
                Serial.printf("[10C0] %s\n", ok ? "OPENED" : "UNEXPECTED");
                return;
            }
            if (waitingPing)
            {
                waitingPing = false;
                waiting = false;
                // 7F 3E 12 is expected; just keep the session fresh
                sessions[currentHeader].lastMs = now;
                Serial.print("[3E00] tester present ack,payload:");
                Serial.println(elm.payload);
                return;
            }

            if (waitingPid)
            {
                waitingPid = false;
                waiting = false;

                // Parse payload -> UDS data (strip 61 xx if present)
                auto data = decodeToUdsData(elm.payload, elm.PAYLOAD_LEN);
                
                const auto &node = plan[planIndex];
#if LOG_PID_VALUES
                Serial.print("[RAW ] ");
                for (size_t i = 0; i < elm.PAYLOAD_LEN; ++i) {
                    unsigned char c = static_cast<unsigned char>(elm.payload[i]);
                    if (c == '\r') Serial.print("\\r");
                    else if (c == '\n') Serial.print("\\n");
                    else if (c == '\t') Serial.print("\\t");
                    else if (c >= 32 && c <= 126) Serial.print((char)c);
                    else Serial.printf("\\x%02X", c);
                }
                Serial.println();
                
                Serial.printf("[RECV] header=%s pid=%s  ->  decoded UDS payload\n",
                              currentHeader.c_str(), node.modePid);
                dumpHex(data);
                Serial.printf("onmodified payload\n"   ); 
#endif
                // Evaluate metrics for this plan node
 
                for (const auto &m : node.metrics)
                {
                    if (!m.eval)
                        continue;
                    float v = m.eval(data);
                    valueCache[String(m.shortName)] = v; // cache latest value

#if LOG_PID_VALUES
                    // pretty print: Name (Short) = value [unit]
                    const char *name = (m.name && m.name[0]) ? m.name : m.shortName;
                    const char *unit = (m.units && m.units[0]) ? m.units : "";
                    Serial.printf("  %-32s (%-8s) = %8.3f %s\n",
                                  name ? name : "-", m.shortName ? m.shortName : "-",
                                  v, unit);
#endif
                }

                sessions[currentHeader].lastMs = now;

                // Advance to next plan node
                planIndex = (planIndex + 1) % plan.size();
                return;
            }
            waiting = false;
            return;
        }
 
        // error: drop and move on_exit

        // TODO: show actual error??
        waiting = waitingHeaderOK = waitingSDS = waitingPing = waitingPid = false;
        Serial.printf("[ELM] rx error state=%d\n", elm.nb_rx_state);
        planIndex = (planIndex + 1) % plan.size();
#if LOG_PID_VALUES
        Serial.printf("[ELM][PID ERR] header=%s pid=%s state=%d\n",
                      currentHeader.c_str(),
                      plan.empty() ? "-" : plan[planIndex].modePid,
                      (int)elm.nb_rx_state); 
#endif

        return;
    }

    // not waiting:

    // B) Not waiting: throttle a bit between commands
    if ((now - lastCmdMs) < kCmdIntervalMs)
    {
        // Serial.println("throttling between commands ...");
        return;
    }
    

    // Serial.println("ready to send next command ...");
    //  C) Choose current plan node
    const auto &node = plan[planIndex];

    // D) Ensure correct header is selected
    if (currentHeader != node.header)
    {
        sendHeaderIfNeeded(node.header);
        delay(20); // give ELM time to process ATSH//TODO:UNBLOCK
        return;
    }

    // E) If this node needs SDS, open (or re-open after idle)
    {
        Sess &s = sessions[currentHeader];
        if (s.open && (now - s.lastMs) > kReopenSdsMs)
        {
            Serial.printf("[SDS] idle >= %u ms, re-open 10C0 (%s)\n", kReopenSdsMs, currentHeader.c_str());
            s.open = false;
        }
        if (node.needsSession && !s.open)
        {
            Serial.println("[SEND] 10C0");
            elm.sendCommand("10C0");
            waiting = true;
            waitingSDS = true;
            lastCmdMs = now;
            return;
        }
    }

    // F) TesterPresent if due
    {
        Sess &s = sessions[currentHeader];
        if ((now - s.lastMs) > kPingPeriodMs)
        {
            Serial.println("[SEND] 3E 00");
            elm.sendCommand("3E00");
            waiting = true;
            waitingPing = true;
            lastCmdMs = now;
            return;
        }
    }
    
    // G) Send the PID
    Serial.printf("[SEND] %s (%s)\n", node.modePid, node.header);
    elm.sendCommand(node.modePid);
    waiting = true;
    waitingPid = true;
    lastCmdMs = now;
}
