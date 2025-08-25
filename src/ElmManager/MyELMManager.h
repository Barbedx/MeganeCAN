#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include <WiFi.h>

#include "ELMduino.h"
#include "display/IDisplay.h"
#include "commands/DisplayCommands.h"

// -------- Data structures --------
struct QueryData
{
    const char *modeAndPid;                         // e.g. "2112", "21C1"
    const char *name;                               // human label
    std::function<float(const uint8_t *)> evalFunc; // value extraction
};
struct PidPlan {
  const char* header;     // "7E0"
  const char* cmd;        // "21A0", "21C1", ...
  bool needSession;       // true for 7E0
  std::vector<Metric> metrics;
};

struct Metric {
  const char* name;  // "ECT", "IAT", "MAP", ...
  const char* unit;  // "°C","mbar","V","km/h","bar"
  std::function<float(const std::vector<uint8_t>&)> eval; // uses letter helper
};
struct QueryGroup
{
    const char *header;             // 3-hex CAN ID, e.g. "743"
    std::vector<QueryData> queries; // all PIDs inside this header
};

// -------- Manager --------
class MyELMManager
{
public:
    MyELMManager(IDisplay &display) : display(display)
    {
        queryList.clear();
std::vector<PidPlan> plan = {
  { "7E0", "21A0", true, {
      // PR_НАПРЯЖЕНИЕ ПИТАНИЯ КОМПЬЮТЕРА = 0.03215*A + 8
      { "ECU_VBAT", "V",   [](auto& b){ return 0.03215f*letterAt(b,'A') + 8.0f; }},
      // PR_ТЕМПЕРАТУРА ВОДЫ = 0.625*B - 40
      { "ECT",      "°C",  [](auto& b){ return 0.625f*letterAt(b,'B') - 40.0f; }},
      // PR_ТЕМПЕРАТУРА ВОЗДУХА = 0.625*C - 40
      { "IAT",      "°C",  [](auto& b){ return 0.625f*letterAt(b,'C') - 40.0f; }},
      // PR_ДАВЛЕНИЕ ВПУСКА = 0.0578*(D*256+E)+103  (мбар)
      { "MAP",      "mbar",[](auto& b){ uint16_t v = (letterAt(b,'D')<<8)|letterAt(b,'E');
                                        return 0.0578f*v + 103.0f; }},
      // PR_СКОРОСТИ АВТОМОБИЛЯ = L (км/ч)
      { "Speed",    "km/h",[](auto& b){ return (float)letterAt(b,'L'); }},
  }},
  { "7E0", "21C1", true, {
      // PR_ДАВЛЕНИЕ ХЛАДАГЕНТА = 2*J/10  (bar)
      { "A/C_Press", "bar",[](auto& b){ return 0.2f*letterAt(b,'J'); }},
  }},
};


    }

    enum QueryState
    {
        SEND_COMMAND,
        WAITING_RESP
    };

    // Call this *each loop*; it will do nothing if already up,
    // otherwise it schedules retries with backoff.
    bool ensureTcpAndElm();

    // 3-hex header (e.g. "743")
    void sendHeader(const char *hex3)
    {
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "AT SH %s", hex3);
        Serial.printf("[Header-SEND] %s\n", cmd);
        elm.sendCommand(cmd); // non-blocking, we'll wait for OK
    }

    // Needs a diag session? (only engine 7E0 for now)
    static bool groupNeedsSession(const QueryGroup &g) {
        return true;
    }


    void tick()
    {
        if (!elmReady)
            return;
        if (queryList.empty())
        {
            Serial.println("[Tick][ERR] queryList is EMPTY");
            return;
        }

        // Bail early if TCP is gone; ensureTcpAndElm() will bring it back next loop
        if (!wifiClient.connected())
        {
            Serial.println("[ELM] TCP dropped; will reconnect");
            elmReady = false;
            return;
        }

        const unsigned long queryInterval = 600; // ms
        if (millis() - lastQueryTime < queryInterval)
            return;

        if (currentGroup >= queryList.size())
        {
            currentGroup = 0;
            currentQuery = 0;

            headerSent = false;       // NEW
            waitingHeaderAck = false; // (nice to clear too)
            sessionOpened = false;
            waitingSessionAck=false;
        }
        const QueryGroup &group = queryList[currentGroup];
        if (group.queries.empty())
        {
            advanceGroup();
            lastQueryTime = millis();
            return;
        }
        if (currentQuery >= group.queries.size())
        {
            currentQuery = 0;
            advanceGroup();
            lastQueryTime = millis();
            return;
        }

        const QueryData &query = group.queries[currentQuery];
 // -------- TRACE STATE SNAPSHOT --------
        // Serial.printf("[State] grp=%u/%u hdr=%s q=%u/%u nb=%s Hsent=%d WHdr=%d SessNeed=%d SessOpen=%d WSess=%d\n",
        //               (unsigned)currentGroup, (unsigned)queryList.size(),
        //               group.header,
        //               (unsigned)currentQuery, (unsigned)group.queries.size(),
        //               nb_query_state==SEND_COMMAND?"SEND":"WAIT",
        //               headerSent, waitingHeaderAck,
        //               groupNeedsSession(group), sessionOpened, waitingSessionAck);

        switch (nb_query_state)
        {
        case SEND_COMMAND:
        {
            //just a ping to show tester presence
            if( millis() - lastTesterMs > testerPeriod) {
                //check if is extended session
                elm.sendCommand("3E 00");
                nb_query_state = WAITING_RESP;
                waitingTesterAck = true;
                lastQueryTime = millis(); 
                lastTesterMs = millis();
                break;
            }

            if (!headerSent && false) //send once only
            {
                // Send header once per group, then wait for OK
                 Serial.printf("[Flow] Sending header for ECU %s\n", group.header);
                sendHeader(group.header);
                waitingHeaderAck = true;
                nb_query_state = WAITING_RESP;
                lastQueryTime = millis();
                return;
            } 
            
            // 2) If this ECU needs a session, open it once: 10 C0 → expect 50 C0
            if ( false && groupNeedsSession(group) && !sessionOpened && !waitingSessionAck) {
                Serial.println("[Flow] Opening diagnostic session: 10C0");
                elm.sendCommand("10 C0");
                waitingSessionAck = true;
                nb_query_state = WAITING_RESP;
                lastQueryTime = millis();
                return;
            }

                Serial.printf("[Send] PID=%s (%s)\n", query.modeAndPid, query.name);
                elm.sendCommand(query.modeAndPid);
                nb_query_state = WAITING_RESP;
                lastQueryTime = millis();
            
            break;
        }

        case WAITING_RESP:
        {
            elm.get_response();

            if (elm.nb_rx_state == ELM_SUCCESS)
            {

                lastGoodRxMs = millis(); // <—— mark liveness on *every* success
                if(waitingTesterAck) {
                    waitingTesterAck = false;
                    Serial.println("[Tester] 3e00 acknowledged");
                    nb_query_state = SEND_COMMAND;
                    lastGoodRxMs = millis();
                    lastQueryTime = millis();
                    return;
                }
                if (waitingHeaderAck)
                {
                    // *** FIX: advance to the FIRST PID after header OK
                    waitingHeaderAck = false;
                    headerSent = true;
                     Serial.printf("[Header->OK] ECU %s acknowledged header\n", group.header);
                   
                    nb_query_state = SEND_COMMAND;
                    // go to first PID if the group has any

                    lastQueryTime = millis();
                    return;
                }

                if (waitingSessionAck) {
                    waitingSessionAck = false;
                    // Minimal check: expect 50 C0 (positive response to 10 C0)
                    bool pos = (elm.PAYLOAD_LEN >= 2 &&
                                elm.payload[0] == 0x50 &&
                                elm.payload[1] == 0xC0);
                    sessionOpened = pos;
                    Serial.printf("[Session] 10C0 reply: %02X %02X ... (%s)\n",
                                  elm.payload[0], elm.payload[1],
                                  pos ? "OPENED" : "UNEXPECTED");
                    nb_query_state = SEND_COMMAND;
                    lastQueryTime = millis();
                    return;
                }
                
                // Normal PID response – dump raw payload
                Serial.print("[Payload] ");
                for (size_t i = 0; i < elm.PAYLOAD_LEN; i++)
                    Serial.printf("%02X ", elm.payload[i]);
                Serial.println();

                Serial.printf("[Response] [%s][%s] (raw only)\n",
                              group.header, query.name);

                // float value = 0.0f;
                // if (query.evalFunc)
                //     value = query.evalFunc((const uint8_t *)elm.payload);

                // Serial.printf("[Response] [%s][%s] = %f\n", group.header, query.name, value);

                nb_query_state = SEND_COMMAND;
                nextQuery(currentGroup, currentQuery);
                lastQueryTime = millis();
            }
            else if (elm.nb_rx_state != ELM_GETTING_MSG)
            {
                Serial.print("Status is: =");
                Serial.println(elm.nb_rx_state);
                static uint8_t consecutiveErrs = 0;
                consecutiveErrs++;

                if (waitingHeaderAck)
                {
                    Serial.printf("[ERR] Header ATSH %s failed or timed out\n", group.header);
                    waitingHeaderAck = false;
                    headerSent = false; // try again next time
                    nb_query_state = SEND_COMMAND;
                    // currentQuery = 0;
                    // advanceGroup();
                }
                else if (waitingSessionAck) {
                    Serial.printf("[ERR] Session 10C0 failed or timed out (state=%d)\n",
                                  elm.nb_rx_state);
                    waitingSessionAck = false;
                    sessionOpened = false;
                    nb_query_state = SEND_COMMAND; // will retry session on next loop
                    } else 
                {
                    Serial.printf("[ERR] Query %s failed or timed out\n", query.modeAndPid);
                    elm.printError();
                    nb_query_state = SEND_COMMAND;
                    nextQuery(currentGroup, currentQuery);
                }
                lastQueryTime = millis();

                if (consecutiveErrs >= 20)
                { // 4 consecutive failures → drop TCP
                    Serial.println("[ELM] Too many errors; dropping TCP...");
                    disconnectTcp();
                    consecutiveErrs = 0;
                    return;
                }
            } 
            break;
        }
        }
    }

    // ------- helpers -------
static uint8_t letterAt(const std::vector<uint8_t>& b, char L) {
  const size_t idx = 2 + (size_t)(L - 'A'); // skip 61 <XX>
  return (idx < b.size()) ? b[idx] : 0;
}

private:
    WiFiClient wifiClient;
    IDisplay &display;
    ELM327 elm;
    IPAddress host = IPAddress(192, 168, 0, 10);
    uint16_t port = 35000;
    uint32_t lastTesterMs = 0;
const uint32_t testerPeriod = 1500; // ms

bool waitingTesterAck = false;
    bool headerSent = false;
    uint32_t nextTcpAttemptMs = 0;
    uint32_t tcpBackoffMs = 1000; // start at 1s
    static constexpr uint32_t kBackoffMaxMs = 30000;

    uint32_t lastGoodRxMs = 0;                    // for liveness
    static constexpr uint32_t kDeadLinkMs = 800000; // if no good RX in 8s, consider dead

    // ---- small helpers ----
    void disconnectTcp()
    {
        wifiClient.stop();
        elmReady = false;
        waitingHeaderAck = false;
    }
    bool connectTcpOnce(uint16_t port);
    bool initElmOnce();

    std::vector<QueryGroup> queryList;

    bool elmReady = false;
    bool waitingHeaderAck = false;

    // NEW: simple per-group session state 
    bool sessionOpened = false;
    bool waitingSessionAck = false;

    QueryState nb_query_state = SEND_COMMAND;
    size_t currentGroup = 0;
    size_t currentQuery = 0;
    unsigned long lastQueryTime = 0;

    void nextQuery(size_t &group, size_t &q)
    {
        q++;
        if (q >= queryList[group].queries.size())
        {
            q = 0;
            advanceGroup();
        }
    }
    void advanceGroup()
    {
        currentGroup++;
        if (currentGroup >= queryList.size())
            currentGroup = 0;
        headerSent = false;       // NEW: force a header next group
        waitingHeaderAck = false; // NEW: be safe

        sessionOpened = false;    // NEW: force a session next group
        waitingSessionAck=false;
            Serial.printf("[Flow] Advance to next group: %s\n", queryList[currentGroup].header);
   
    }
};
