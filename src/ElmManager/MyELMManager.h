#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include <WiFi.h>

#include "ELMduino.h"
#include "display/IDisplay.h"
#include "commands/DisplayCommands.h"

// -------- Data structures --------
struct QueryData {
    const char *modeAndPid;                         // e.g. "2112", "21C1"
    const char *name;                               // human label
    std::function<float(const uint8_t *)> evalFunc; // value extraction
};

struct QueryGroup {
    const char *header;             // 3-hex CAN ID, e.g. "743"
    std::vector<QueryData> queries; // all PIDs inside this header
};

// -------- Manager --------
class MyELMManager {
public:
    explicit MyELMManager(IDisplay &display) : display(display) {
        queryList.clear();

        // 74D
        {
            QueryGroup g;
            g.header = "74D";
            g.queries.push_back({"2102",
                                 "PR_ЗАРЯД ОТ ГЕНЕРАТОРА",
                                 [](const uint8_t *p) {
                                     return 0.390625f * MyELMManager::getByte(p, 'A');
                                 }});
            queryList.push_back(g);
        }

        // 743
        {
            QueryGroup g;
            g.header = "743";
            g.queries.push_back({"2112",
                                 "PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В",
                                 [](const uint8_t *p) {
                                     return (MyELMManager::getByte(p, 'V') * 256
                                             + MyELMManager::getByte(p, 'W')) / 1000.0f;
                                 }});
            g.queries.push_back({"2112",
                                 "PR_НАРУЖНАЯ ТЕМПЕРАТУРА",
                                 [](const uint8_t *p) {
                                     return MyELMManager::getByte(p, 'Z') - 40.0f;
                                 }});
            queryList.push_back(g);
        }

        // 744
        {
            QueryGroup g;
            g.header = "744";
            g.queries.push_back({"21C1",
                                 "PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В",
                                 [](const uint8_t *p) {
                                     return 0.078f * MyELMManager::getByte(p, 'J');
                                 }});
            g.queries.push_back({"21C1",
                                 "PR_НАРУЖНАЯ ТЕМПЕРАТУРА",
                                 [](const uint8_t *p) {
                                     return MyELMManager::getByte(p, 'B') - 40.0f;
                                 }});
            queryList.push_back(g);
        }

        // 745
        {
            QueryGroup g;
            g.header = "745";
            g.queries.push_back({"21C1",
                                 "PR_НАПРЯЖЕНИЕ АККУМУЛЯТОРНОЙ БАТАРЕИ 12 В",
                                 [](const uint8_t *p) {
                                     return MyELMManager::getByte(p, 'B') * 0.0625f;
                                 }});
            g.queries.push_back({"21C1",
                                 "PR_НАРУЖНАЯ ТЕМПЕРАТУРА",
                                 [](const uint8_t *p) {
                                     return MyELMManager::getByte(p, 'A') - 40.0f;
                                 }});
            g.queries.push_back({"2113",
                                 "ST_ДАТЧИК НАРУЖНОЙ ТЕМПЕРАТУРЫ",
                                 [](const uint8_t *p) {
                                     return MyELMManager::getByte(p, 'U');
                                 }});
            queryList.push_back(g);
        }
    }

    enum QueryState { SEND_COMMAND, WAITING_RESP };

    // Call once after WiFi STA is connected to V-LINK
    void setup() {
        IPAddress host(192, 168, 0, 10);
        const uint16_t ports[] = {35000, 23};

        wifiClient.stop();
        bool ok = false;

        for (uint8_t i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i) {
            Serial.printf("[ELM] Connecting TCP %s:%u ...\n",
                          host.toString().c_str(), ports[i]);
            if (wifiClient.connect(host, ports[i])) { ok = true; break; }
        }
        if (!ok) {
            Serial.println("[ELM] TCP connect FAILED on all ports");
            elmReady = false;
            return;
        }

        Serial.println("[ELM] TCP connected");
        wifiClient.setTimeout(5000);

        elm.begin(wifiClient, true, 2000);   // echoOffOnInit = true, 2s timeout

        // Robust init
        elm.sendCommand_Blocking("ATD");     // defaults
        elm.sendCommand_Blocking("ATZ");     // reset
        elm.sendCommand_Blocking("ATE0");    // echo off
        elm.sendCommand_Blocking("ATS0");    // no spaces
        elm.sendCommand_Blocking("ATL0");    // no linefeeds
        elm.sendCommand_Blocking("ATAL");    // allow long
        elm.sendCommand_Blocking("ATST 64"); // shorter timeout (opt)
        elm.sendCommand_Blocking("ATSP6");   // ISO 15765-4 CAN (11bit 500k)

        elmReady = true;
        nb_query_state = SEND_COMMAND;
        currentGroup = currentQuery = 0;
        lastQueryTime = 0;
        waitingHeaderAck = false;
    }

    // 3-hex header (e.g. "743")
    void sendHeader(const char *hex3) {
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "ATSH %s", hex3);
        Serial.printf("[Header] %s\n", cmd);
        elm.sendCommand(cmd);   // non-blocking, we'll wait for OK
    }

    void tick() {
        if (!elmReady) return;
        if (queryList.empty()) {
            Serial.println("[Tick][ERR] queryList is EMPTY");
            return;
        }

        const unsigned long queryInterval = 600; // ms
        if (millis() - lastQueryTime < queryInterval) return;

        if (currentGroup >= queryList.size()) {
            currentGroup = 0; currentQuery = 0;
        }
        const QueryGroup &group = queryList[currentGroup];
        if (group.queries.empty()) {
            advanceGroup();
            lastQueryTime = millis();
            return;
        }
        if (currentQuery >= group.queries.size()) {
            currentQuery = 0;
            advanceGroup();
            lastQueryTime = millis();
            return;
        }

        const QueryData &query = group.queries[currentQuery];

        switch (nb_query_state) {
        case SEND_COMMAND: {
            if (currentQuery == 0) {
                // send header then wait its OK
                sendHeader(group.header);
                waitingHeaderAck = true;
                nb_query_state = WAITING_RESP;
                lastQueryTime = millis();
                return;
            } else {
                Serial.printf("[Send] PID=%s (%s)\n", query.modeAndPid, query.name);
                elm.sendCommand(query.modeAndPid);
                nb_query_state = WAITING_RESP;
                lastQueryTime = millis();
            }
            break;
        }

        case WAITING_RESP: {
            elm.get_response();

            if (elm.nb_rx_state == ELM_SUCCESS) {
                if (waitingHeaderAck) {
                    // *** FIX: advance to the FIRST PID after header OK
                    waitingHeaderAck = false;
                    nb_query_state = SEND_COMMAND;
                    // go to first PID if the group has any
                    if (group.queries.size() > 0) currentQuery = 1;
                    lastQueryTime = millis();
                    return;
                }

                Serial.print("[Payload] ");
                for (size_t i = 0; i < elm.PAYLOAD_LEN; i++) Serial.printf("%02X ", elm.payload[i]);
                Serial.println();

                float value = 0.0f;
                if (query.evalFunc) value = query.evalFunc((const uint8_t *)elm.payload);

                Serial.printf("[Response] [%s][%s] = %f\n", group.header, query.name, value);

                nb_query_state = SEND_COMMAND;
                nextQuery(currentGroup, currentQuery);
                lastQueryTime = millis();
            }
            else if (elm.nb_rx_state != ELM_GETTING_MSG) {
                if (waitingHeaderAck) {
                    Serial.printf("[ERR] Header ATSH %s failed or timed out\n", group.header);
                    waitingHeaderAck = false;
                    nb_query_state = SEND_COMMAND;
                    currentQuery = 0;
                    advanceGroup();
                } else {
                    Serial.printf("[ERR] Query %s failed or timed out\n", query.modeAndPid);
                    elm.printError();
                    nb_query_state = SEND_COMMAND;
                    nextQuery(currentGroup, currentQuery);
                }
                lastQueryTime = millis();
            }
            break;
        }
        }
    }

    // ------- helpers -------
    static uint8_t getByte(const uint8_t *p, char symbol) {
        switch (symbol) {
            case 'A': return p[0];
            case 'B': return p[1];
            case 'C': return p[2];
            case 'D': return p[3];
            case 'E': return p[4];
            case 'F': return p[5];
            case 'G': return p[6];
            case 'H': return p[7];
            case 'I': return p[8];
            case 'J': return p[9];
            case 'K': return p[10];
            case 'L': return p[11];
            case 'M': return p[12];
            case 'N': return p[13];
            case 'O': return p[14];
            case 'P': return p[15];
            case 'U': return p[20];
            case 'V': return p[21];
            case 'W': return p[22];
            case 'Z': return p[25];
        }
        return 0;
    }

private:
    WiFiClient wifiClient;
    IDisplay &display;
    ELM327 elm;

    std::vector<QueryGroup> queryList;

    bool elmReady = false;
    bool waitingHeaderAck = false;

    QueryState nb_query_state = SEND_COMMAND;
    size_t currentGroup = 0;
    size_t currentQuery = 0;
    unsigned long lastQueryTime = 0;

    void nextQuery(size_t &group, size_t &q) {
        q++;
        if (q >= queryList[group].queries.size()) {
            q = 0;
            advanceGroup();
        }
    }
    void advanceGroup() {
        currentGroup++;
        if (currentGroup >= queryList.size()) currentGroup = 0;
    }
};
