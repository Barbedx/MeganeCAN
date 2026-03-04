#include "MyELMManager.h"
#include <Arduino.h>
using std::vector;

// ---- WiFi / TCP / ELM init ----

bool MyELMManager::ensureWifi()
{
    if (WiFi.status() == WL_CONNECTED) return true;
    Serial.println("[ELM] WiFi not connected, reconnecting...");
    WiFi.reconnect();
    return false;
}

bool MyELMManager::connectTcpOnce(uint16_t p)
{
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

void MyELMManager::disconnectTcp()
{
    wifiClient.stop();
    elmReady      = false;
    waitState     = WaitState::IDLE;
    currentHeader = "";
}

bool MyELMManager::initElmOnce()
{
    elm.begin(wifiClient, /*debug*/ false, /*timeout*/ 5000,
              ISO_15765_11_BIT_500_KBAUD, /*rxBuf*/ 256, /*dataTimeout*/ 225);

    // Lock protocol to ISO15765-4 11-bit 500K; let tick() handle ATSH + 10C0
    elm.sendCommand_Blocking("AT SP 6");
    Serial.println("[ELM] init done");

    elmReady      = true;
    waitState     = WaitState::IDLE;
    planIndex     = 0;
    lastGoodRxMs  = millis();
    tcpBackoffMs  = 1000;
    currentHeader = "";
    sessions.clear();
    return true;
}

bool MyELMManager::ensureTcpAndElm()
{
    const uint32_t now = millis();

    if (!ensureWifi()) return false;

    if (!wifiClient.connected()) {
        if (now < nextTcpAttemptMs) return false;
        if (connectTcpOnce(port) && initElmOnce()) return true;
        disconnectTcp();
        nextTcpAttemptMs = now + tcpBackoffMs;
        tcpBackoffMs = std::min<uint32_t>(tcpBackoffMs * 2, kBackoffMaxMs);
        Serial.printf("[ELM] Reconnect in %u ms\n", tcpBackoffMs);
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
        Serial.println("[ELM] Link dead (no good RX). Reconnecting...");
        disconnectTcp();
        return false;
    }

    return true;
}

// ---- Hex decode ----

static inline bool isHexChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
static inline uint8_t hexNibble(char c)
{
    if (c >= '0' && c <= '9') return uint8_t(c - '0');
    if (c >= 'A' && c <= 'F') return uint8_t(10 + c - 'A');
    return uint8_t(10 + c - 'a');
}

vector<uint8_t> MyELMManager::decodeToUdsData(const char *ascii, size_t len)
{
    String s;
    s.reserve(len);
    for (size_t i = 0; i < len; i++) s += char(ascii[i]);

    // Join hex data: use text after ':' on framed lines ("0: AA BB ..."),
    // or the whole line for single-frame responses.
    String hexJoined;
    int start = 0;
    while (start < (int)s.length()) {
        int end = s.indexOf('\r', start);
        if (end < 0) end = s.length();
        String line = s.substring(start, end);
        line.trim();
        int colon = line.indexOf(':');
        hexJoined += (colon >= 0) ? line.substring(colon + 1)
                                  : (line.length() >= 4 ? line : "");
        start = end + 1;
    }

    // Strip spaces
    String noSpace;
    noSpace.reserve(hexJoined.length());
    for (int i = 0; i < (int)hexJoined.length(); ++i) {
        char c = hexJoined[i];
        if (c != ' ' && c != '\t') noSpace += c;
    }

    // Hex pairs → bytes
    vector<uint8_t> raw;
    int i = 0;
    while (i + 1 < (int)noSpace.length()) {
        if (isHexChar(noSpace[i]) && isHexChar(noSpace[i + 1])) {
            raw.push_back((hexNibble(noSpace[i]) << 4) | hexNibble(noSpace[i + 1]));
            i += 2;
        } else {
            i += 1;
        }
    }

    return udsDataOnly(raw); // strip 61/62/50 xx header if present
}

// ----------------- header enable/disable -----------------
std::vector<String> MyELMManager::getUniqueHeaders() const {
    std::vector<String> result;
    for (const auto& node : plan) {
        String hdr(node.header);
        bool found = false;
        for (const auto& h : result) {
            if (h == hdr) { found = true; break; }
        }
        if (!found) result.push_back(hdr);
    }
    return result;
}

void MyELMManager::setHeaderEnabled(const char* header, bool enabled) {
    _headerEnabled[String(header)] = enabled;
}

bool MyELMManager::isHeaderEnabled(const char* header) const {
    auto it = _headerEnabled.find(String(header));
    return (it == _headerEnabled.end()) ? true : it->second;
}

void MyELMManager::loadHeaderConfig(Preferences& prefs) {
    prefs.begin("elmcfg", true);
    for (const auto& hdr : getUniqueHeaders()) {
        String key = "h_" + hdr;
        _headerEnabled[hdr] = prefs.getBool(key.c_str(), true);
    }
    prefs.end();
}

void MyELMManager::saveHeaderConfig(Preferences& prefs) const {
    prefs.begin("elmcfg", false);
    for (const auto& kv : _headerEnabled) {
        String key = "h_" + kv.first;
        prefs.putBool(key.c_str(), kv.second);
    }
    prefs.end();
}

String MyELMManager::headersJson() const {
    String out = "{";
    bool first = true;
    for (const auto& hdr : getUniqueHeaders()) {
        if (!first) out += ",";
        first = false;
        out += "\""; out += hdr; out += "\":";
        out += isHeaderEnabled(hdr.c_str()) ? "true" : "false";
    }
    out += "}";
    return out;
}

// ----------------- metric snapshots -----------------
std::vector<MetricSnapshot> MyELMManager::getCachedMetrics() const {
    std::vector<MetricSnapshot> out;
    for (const auto& node : plan)
        for (const auto& m : node.metrics) {
            MetricSnapshot s;
            s.shortName = String(m.shortName);
            s.label     = (m.label && m.label[0]) ? String(m.label) : String(m.shortName);
            s.unit      = m.units ? String(m.units) : String("");
            auto it = valueCache.find(s.shortName);
            if (it != valueCache.end()) { s.value = it->second; s.hasValue = true; }
            out.push_back(s);
        }
    return out;
}

std::vector<MetricSnapshot> MyELMManager::getCachedMetrics(const String& header) const {
    std::vector<MetricSnapshot> out;
    for (const auto& node : plan) {
        if (String(node.header) != header) continue;
        for (const auto& m : node.metrics) {
            MetricSnapshot s;
            s.shortName = String(m.shortName);
            s.label     = (m.label && m.label[0]) ? String(m.label) : String(m.shortName);
            s.unit      = m.units ? String(m.units) : String("");
            auto it = valueCache.find(s.shortName);
            if (it != valueCache.end()) { s.value = it->second; s.hasValue = true; }
            out.push_back(s);
        }
    }
    return out;
}

// ----------------- on-demand scan -----------------
bool MyELMManager::requestScan(const char* header, const char* pid) {
    if (_scanMode || _scanPending) return false;

    _scanHeader = header;
    _scanPid    = pid;

    // needsSession: true if ANY plan node for this header requires it
    _scanNeedsSession = false;
    for (const auto& n : plan)
        if (String(n.header) == _scanHeader && n.needsSession)
            { _scanNeedsSession = true; break; }

    // Copy metrics if this exact (header, pid) is in the plan
    _scanMetrics.clear();
    for (const auto& n : plan)
        if (String(n.header) == _scanHeader && String(n.modePid) == _scanPid)
            { _scanMetrics = n.metrics; break; }

    _scanResult = ScanResult{};
    // drain any stale signal before arming
    xSemaphoreTake(_scanDoneSem, 0);
    _scanPending = true;
    return true;
}

bool MyELMManager::waitScan(uint32_t timeoutMs) {
    return xSemaphoreTake(_scanDoneSem, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void MyELMManager::cancelScan() {
    _scanPending = false;
    _scanMode    = false;
}

String MyELMManager::planJson() const {
    // Build {"7E0":["21A0","21C1"],"743":[...],...}
    std::map<String, std::vector<String>> byHeader;
    for (const auto& n : plan)
        byHeader[String(n.header)].push_back(String(n.modePid));

    String out = "{";
    bool firstH = true;
    for (const auto& kv : byHeader) {
        if (!firstH) out += ",";
        firstH = false;
        out += "\"" + kv.first + "\":[";
        bool firstP = true;
        for (const auto& p : kv.second) {
            if (!firstP) out += ",";
            firstP = false;
            out += "\"" + p + "\"";
        }
        out += "]";
    }
    out += "}";
    return out;
}

String MyELMManager::computeMetricsJson(const std::vector<MetricDef>& metrics,
                                         const std::vector<uint8_t>& data) const {
    if (metrics.empty()) return "{}";
    String out = "{";
    bool first = true;
    for (const auto& m : metrics) {
        if (!m.eval) continue;
        float v = m.eval(data);
        if (!first) out += ",";
        first = false;
        out += "\""; out += m.shortName; out += "\":"; out += String(v, 3);
        if (m.units && m.units[0]) {
            out += ",\""; out += m.shortName; out += "_unit\":\""; out += m.units; out += "\"";
        }
    }
    out += "}";
    return out;
}

// ---- Debug ----

#ifndef LOG_PID_VALUES
#define LOG_PID_VALUES 1
#endif

static void dumpHex(const vector<uint8_t> &v)
{
#if LOG_PID_VALUES
    Serial.print("[DATA] ");
    for (uint8_t b : v) Serial.printf("%02X ", b);
    Serial.printf("(%u bytes)\n", (unsigned)v.size());
#endif
}

// ---- Main state machine ----

void MyELMManager::tick()
{
    if (!ensureTcpAndElm()) return;
    if (plan.empty()) return;

    const uint32_t now = millis();

    // ---- A) Process pending response ----
    if (waitState != WaitState::IDLE)
    {
        elm.get_response();

        if (elm.nb_rx_state == ELM_GETTING_MSG)
            return;

        if (elm.nb_rx_state != ELM_SUCCESS)
        {
            // Log the failing PID *before* advancing the index
            Serial.printf("[ELM] rx error state=%d header=%s pid=%s\n",
                          (int)elm.nb_rx_state,
                          currentHeader.c_str(),
                          plan.empty() ? "-" : plan[planIndex].modePid);

            // Fail an in-progress scan
            if (_scanMode) {
                _scanResult.errorMsg = "ELM error state=" + String(elm.nb_rx_state);
                _scanResult.ready    = true;
                _scanMode            = false;
                xSemaphoreGive(_scanDoneSem);
                waitState = WaitState::IDLE;
                return;
            }

            if (waitState == WaitState::PID)
                planIndex = (planIndex + 1) % plan.size();
            waitState = WaitState::IDLE;
            return;
        }

        // --- ELM_SUCCESS ---
        lastGoodRxMs = now;

        switch (waitState)
        {
        case WaitState::HEADER:
            currentHeader  = pendingHeader;
            pendingHeader  = "";
            Serial.printf("[RECV] OK for ATSH %s\n", currentHeader.c_str());
            break;

        case WaitState::SDS:
        {
            auto uds = decodeToUdsData(elm.payload, elm.PAYLOAD_LEN);
            // Positive response (50 C0 ...) is stripped by udsDataOnly → data bytes remain.
            // Negative response (7F xx ...) is NOT stripped → uds[0] == 0x7F.
            const bool ok = !(uds.size() >= 1 && uds[0] == 0x7F);
            sessions[currentHeader].open   = ok;
            sessions[currentHeader].lastMs = now;
            Serial.printf("[10C0] %s\n", ok ? "OPENED" : "REFUSED");
            break;
        }

        case WaitState::TESTER_PRESENT:
            sessions[currentHeader].lastMs = now;
            break;

        case WaitState::PID:
        {
            auto data = decodeToUdsData(elm.payload, elm.PAYLOAD_LEN);

            // One-shot scan: capture result and wake HTTP task
            if (_scanMode) {
                _scanResult.bytes       = data;
                _scanResult.metricsJson = computeMetricsJson(_scanMetrics, data);
                _scanResult.ready       = true;
                _scanMode               = false;
                sessions[currentHeader].lastMs = now;
                xSemaphoreGive(_scanDoneSem);
                waitState = WaitState::IDLE;
                return;
            }

            const auto &node = plan[planIndex];
#if LOG_PID_VALUES
            Serial.printf("[RECV] header=%s pid=%s\n", currentHeader.c_str(), node.modePid);
            dumpHex(data);
#endif
            for (const auto &m : node.metrics) {
                if (!m.eval) continue;
                float v = m.eval(data);
                valueCache[String(m.shortName)] = v;
#if LOG_PID_VALUES
                Serial.printf("  %-32s (%-8s) = %8.3f %s\n",
                              m.name && m.name[0] ? m.name : m.shortName,
                              m.shortName ? m.shortName : "-",
                              v, m.units && m.units[0] ? m.units : "");
#endif
            }
            sessions[currentHeader].lastMs = now;
            planIndex = (planIndex + 1) % plan.size();
            break;
        }

        default: break;
        }

        waitState = WaitState::IDLE;
        return;
    }

    // ---- B) Rate-limit between commands ----
    if ((now - lastCmdMs) < kCmdIntervalMs)
        return;

    // ---- C) Activate pending scan if any; choose active node params ----
    if (_scanPending) {
        _scanMode    = true;
        _scanPending = false;
        currentHeader = "";                     // force ATSH for scan header
        sessions[_scanHeader].open = false;     // force session reopen
    }

    const char* activeHeader;
    const char* activePid;
    bool        activeNeedsSession;

    if (_scanMode) {
        activeHeader       = _scanHeader.c_str();
        activePid          = _scanPid.c_str();
        activeNeedsSession = _scanNeedsSession;
    } else {
        // ---- Skip disabled or non-focus headers ----
        size_t skipped = 0;
        while (skipped < plan.size()) {
            bool enabled  = isHeaderEnabled(plan[planIndex].header);
            bool inFocus  = _focusHeader.isEmpty() ||
                            String(plan[planIndex].header) == _focusHeader;
            if (enabled && inFocus) break;
            planIndex = (planIndex + 1) % plan.size();
            ++skipped;
        }
        if (skipped == plan.size()) return; // all headers disabled/non-focus
        activeHeader       = plan[planIndex].header;
        activePid          = plan[planIndex].modePid;
        activeNeedsSession = plan[planIndex].needsSession;
    }

    // ---- D) Ensure correct CAN header ----
    if (currentHeader != activeHeader)
    {
        pendingHeader = activeHeader;
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "ATSH %s", activeHeader);
        Serial.printf("[SEND] %s\n", cmd);
        elm.sendCommand(cmd);
        waitState = WaitState::HEADER;
        lastCmdMs = now;
        return;
    }

    Sess &s = sessions[currentHeader];

    // ---- E) Open / reopen diagnostic session if this PID needs one ----
    if (activeNeedsSession)
    {
        if (s.open && (now - s.lastMs) > kReopenSdsMs) {
            Serial.printf("[SDS] idle>=%ums, re-open (%s)\n", kReopenSdsMs, currentHeader.c_str());
            s.open = false;
        }
        if (!s.open) {
            Serial.printf("[SEND] 10C0 (%s)\n", currentHeader.c_str());
            elm.sendCommand("10C0");
            waitState = WaitState::SDS;
            lastCmdMs = now;
            return;
        }
    }

    // ---- F) TesterPresent to keep the session alive ----
    //         Only send when there is actually an open session to keep alive.
    //         Skipped during scan to avoid extra round-trips.
    if (!_scanMode && s.open && (now - s.lastMs) > kPingPeriodMs)
    {
        Serial.printf("[SEND] 3E00 (%s)\n", currentHeader.c_str());
        elm.sendCommand("3E00");
        waitState = WaitState::TESTER_PRESENT;
        lastCmdMs = now;
        return;
    }

    // ---- G) Send the PID ----
    Serial.printf("[SEND] %s (%s)\n", activePid, activeHeader);
    elm.sendCommand(activePid);
    waitState = WaitState::PID;
    lastCmdMs = now;
}
